/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "compile/SpecCodegen.h"
#include <sstream>
#include "gpu/aot/AOTResourceProfile.h"
#include <cctype>
#include <map>
#include "core/DiagnosticCodes.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/Uniform.h"

namespace tgfx {

namespace {

constexpr const char* kSpecMarker = "#pragma tgfx_spec_include";
constexpr const char* kSpecCallPrefix = "SPEC_";
constexpr const char* kSpecActiveName = "ACTIVE";

bool IsIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/// Removes // line comments and /* */ block comments, keeping the newlines so the reported line
/// numbers stay true. The shader sources carry no string literals, so a text-level strip is safe.
std::string StripComments(const std::string& source) {
  std::string result;
  result.reserve(source.size());
  size_t i = 0;
  while (i < source.size()) {
    if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/') {
      while (i < source.size() && source[i] != '\n') {
        ++i;
      }
    } else if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) {
        if (source[i] == '\n') {
          result += '\n';
        }
        ++i;
      }
      i = i + 2 <= source.size() ? i + 2 : source.size();
    } else {
      result += source[i];
      ++i;
    }
  }
  return result;
}

void ReportSpecDiagnostic(Diagnostics* diagnostics, const char* code, const std::string& shader,
                          uint32_t fragIndex, const std::string& message) {
  if (diagnostics == nullptr) {
    return;
  }
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Error;
  diagnostic.layer = Layer::Compile;
  diagnostic.code = code;
  diagnostic.shader = shader;
  diagnostic.fragPermutationIndex = static_cast<int>(fragIndex);
  diagnostic.message = message;
  diagnostics->report(std::move(diagnostic));
}

/// Whether one line of the comment-stripped source declares the uniform `name`: its trimmed form
/// starts with the type keyword and names the member, which is how every UBO declaration in the
/// kernels is spelled (`int SlotCount;`, `ivec4 SlotPacked[CHAIN_SLOT_CAPACITY];`).
bool LineDeclaresUniform(const std::string& line, const std::string& name) {
  auto first = line.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return false;
  }
  auto trimmed = line.substr(first);
  for (const auto* type : {"int ", "ivec4 "}) {
    if (trimmed.compare(0, std::char_traits<char>::length(type), type) == 0) {
      auto rest = trimmed.substr(std::char_traits<char>::length(type));
      return rest.compare(0, name.size(), name) == 0 &&
             (rest.size() == name.size() || !IsIdentifierChar(rest[name.size()]));
    }
  }
  return false;
}

size_t LineOfOffset(const std::string& text, size_t offset) {
  size_t line = 1;
  for (size_t i = 0; i < offset && i < text.size(); ++i) {
    if (text[i] == '\n') {
      ++line;
    }
  }
  return line;
}

std::string LineAt(const std::string& text, size_t offset) {
  auto start = text.rfind('\n', offset);
  start = start == std::string::npos ? 0 : start + 1;
  auto end = text.find('\n', offset);
  end = end == std::string::npos ? text.size() : end;
  return text.substr(start, end - start);
}

}  // namespace

SpecUsage ScanSpecUsage(const std::string& fragmentSource, const std::string& shaderName,
                        Diagnostics* diagnostics) {
  SpecUsage usage;
  const auto stripped = StripComments(fragmentSource);
  for (size_t i = 0; i < stripped.size(); ++i) {
    if (stripped.compare(i, std::char_traits<char>::length(kSpecMarker), kSpecMarker) == 0) {
      usage.present = true;
      break;
    }
  }
  std::map<std::string, bool> seen;
  for (size_t i = 0; i < stripped.size(); ++i) {
    if (stripped.compare(i, 5, kSpecCallPrefix) != 0) {
      continue;
    }
    // The SPEC_ prefix must start an identifier of its own: TGFX_SPEC_* helper macro names
    // (the tool's own generated vocabulary) carry the same five letters mid-identifier.
    if (i > 0 && IsIdentifierChar(stripped[i - 1])) {
      continue;
    }
    auto nameStart = i + 5;
    auto nameEnd = nameStart;
    while (nameEnd < stripped.size() && IsIdentifierChar(stripped[nameEnd])) {
      ++nameEnd;
    }
    auto name = stripped.substr(nameStart, nameEnd - nameStart);
    if (name.empty()) {
      continue;
    }
    auto call = nameEnd;
    while (call < stripped.size() && (stripped[call] == ' ' || stripped[call] == '\t')) {
      ++call;
    }
    bool indexed = false;
    if (call < stripped.size() && stripped[call] == '(') {
      auto argument = call + 1;
      while (argument < stripped.size() &&
             (stripped[argument] == ' ' || stripped[argument] == '\t' ||
              stripped[argument] == '\n' || stripped[argument] == '\r')) {
        ++argument;
      }
      indexed = argument < stripped.size() && stripped[argument] != ')';
    }
    if (name == kSpecActiveName) {
      i = nameEnd;
      continue;
    }
    if (seen.find(name) == seen.end()) {
      seen.emplace(name, indexed);
      usage.fields.push_back(SpecUsageField{name, indexed});
    }
    i = nameEnd;
  }
  if (!usage.present || usage.fields.empty()) {
    return usage;
  }
  // The raw-read check: a field name standing on its own (the SPEC_ prefix is part of the same
  // identifier, so a call site never matches) must appear on a declaration line only — anywhere
  // else it reads the uniform directly and escapes the specialized folding.
  for (const auto& field : usage.fields) {
    size_t offset = 0;
    while (offset < stripped.size()) {
      auto hit = stripped.find(field.name, offset);
      if (hit == std::string::npos) {
        break;
      }
      offset = hit + field.name.size();
      const bool wordBoundaryBefore =
          hit == 0 || !IsIdentifierChar(stripped[hit - 1]);
      const bool wordBoundaryAfter =
          offset >= stripped.size() || !IsIdentifierChar(stripped[offset]);
      if (!wordBoundaryBefore || !wordBoundaryAfter) {
        continue;
      }
      if (LineDeclaresUniform(LineAt(stripped, hit), field.name)) {
        continue;
      }
      ReportSpecDiagnostic(
          diagnostics, DiagnosticCode::SpecRawRead, shaderName, 0,
          "uniform '" + field.name + "' is read directly at line " +
              std::to_string(LineOfOffset(stripped, hit)) + " instead of through SPEC_" +
              field.name + (field.indexed ? "(i)" : "()") +
              "; a direct read escapes the specialized folding: " + LineAt(stripped, hit));
    }
  }
  return usage;
}

std::vector<SpecEntry> MakeSpecEntries(const SpecUsage& usage,
                                       const StageReflectionData& reflection,
                                       const std::string& shaderName, uint32_t fragIndex,
                                       Diagnostics* diagnostics) {
  std::vector<SpecEntry> entries;
  if (!usage.present) {
    return entries;
  }
  uint16_t nextId = 1;  // 0 is the master switch.
  for (const auto& field : usage.fields) {
    const UniformEntry* found = nullptr;
    for (const auto& uniform : reflection.uniforms) {
      if (uniform.name == field.name) {
        found = &uniform;
        break;
      }
    }
    if (found == nullptr) {
      ReportSpecDiagnostic(diagnostics, DiagnosticCode::SpecUnknownField, shaderName, fragIndex,
                           "SPEC_" + field.name + " has no member '" + field.name +
                               "' in the fragment uniform block");
      continue;
    }
    const auto format = static_cast<UniformFormat>(found->format);
    if (field.indexed && format != UniformFormat::Int4) {
      ReportSpecDiagnostic(diagnostics, DiagnosticCode::SpecUnknownField, shaderName, fragIndex,
                           "SPEC_" + field.name + "(i) indexes a member that is not an ivec4 array");
      continue;
    }
    if (!field.indexed && format != UniformFormat::Int) {
      ReportSpecDiagnostic(diagnostics, DiagnosticCode::SpecUnknownField, shaderName, fragIndex,
                           "SPEC_" + field.name + "() reads a member that is not an int");
      continue;
    }
    if (found->arraySize == 0 || found->arraySize > 1024) {
      ReportSpecDiagnostic(diagnostics, DiagnosticCode::SpecUnknownField, shaderName, fragIndex,
                           "member '" + field.name + "' has an unusable array size " +
                               std::to_string(found->arraySize));
      continue;
    }
    SpecEntry entry;
    entry.name = field.name;
    entry.format = found->format;
    entry.arraySize = found->arraySize;
    entry.firstId = nextId;
    nextId = static_cast<uint16_t>(nextId + (field.indexed ? found->arraySize * 4u : 1u));
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::string GenerateSpecInclude(const std::vector<SpecEntry>& entries, bool specDialect) {
  std::string text;
  text += "// Generated by shader_build_tool (compile/SpecCodegen); do not edit.\n";
  text += "// The master switch first: 0 (the default) keeps the generic behavior reading the\n";
  text += "// uniforms; the runtime injects 1 plus the field values per draw shape.\n";
  text += "#define TGFX_SPEC_UNROLLED_EXECUTOR 1\n";
  if (specDialect) {
    text += "layout(constant_id = 0) const int TGFX_SPEC_0 = 0;\n";
  } else {
    text += "const int TGFX_SPEC_0 = 0;\n";
  }
  for (const auto& entry : entries) {
    for (uint32_t id = entry.firstId;
         id < entry.firstId + static_cast<uint32_t>(entry.arraySize) * (entry.arraySize > 1 ? 4u : 1u);
         ++id) {
      if (specDialect) {
        text += "layout(constant_id = " + std::to_string(id) + ") const int TGFX_SPEC_" +
                std::to_string(id) + " = 0;\n";
      } else {
        text += "const int TGFX_SPEC_" + std::to_string(id) + " = 0;\n";
      }
    }
  }
  text += "bool SPEC_ACTIVE() { int s = TGFX_SPEC_0; return s == 1; }\n";
  for (const auto& entry : entries) {
    if (entry.arraySize == 1) {
      text += "int SPEC_" + entry.name + "() { int s = TGFX_SPEC_0; if (s == 1) { return TGFX_SPEC_" +
              std::to_string(entry.firstId) + "; } return " + entry.name + "; }\n";
      continue;
    }
    text += "ivec4 SPEC_" + entry.name + "(int i) {\n";
    text += "  int s = TGFX_SPEC_0;\n";
    text += "  if (s == 1) {\n";
    for (uint32_t element = 0; element < entry.arraySize; ++element) {
      auto base = entry.firstId + element * 4u;
      text += "    if (i == " + std::to_string(element) + ") { int a = TGFX_SPEC_" +
              std::to_string(base) + "; int b = TGFX_SPEC_" + std::to_string(base + 1) +
              "; int c = TGFX_SPEC_" + std::to_string(base + 2) + "; int d = TGFX_SPEC_" +
              std::to_string(base + 3) + "; return ivec4(a, b, c, d); }\n";
    }
    text += "    return ivec4(" + std::to_string(Op::None) + ", -2, -2, 0);\n";
    text += "  }\n";
    text += "  return " + entry.name + "[i];\n";
    text += "}\n";
  }
  return text;
}

std::string GenerateSpecStubInclude(const SpecUsage& usage) {
  std::string text;
  text += "// Generated by shader_build_tool (compile/SpecCodegen); the stub pass's pass-throughs.\n";
  text += "#define SPEC_ACTIVE() false\n";
  // The stub exists for the reflection and the inline policy, so it compiles the loop form
  // alone: the unrolled executor is the specialization machinery the real forms carry.
  text += "#define TGFX_SPEC_UNROLLED_EXECUTOR 0\n";
  for (const auto& field : usage.fields) {
    if (field.indexed) {
      text += "#define SPEC_" + field.name + "(i) " + field.name + "[i]\n";
    } else {
      text += "#define SPEC_" + field.name + "() " + field.name + "\n";
    }
  }
  return text;
}

std::string ReplaceSpecMarker(const std::string& source, const std::string& includeText) {
  auto markerPos = source.find(kSpecMarker);
  if (markerPos == std::string::npos) {
    return source;
  }
  auto lineStart = source.rfind('\n', markerPos);
  lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
  auto lineEnd = source.find('\n', markerPos);
  lineEnd = lineEnd == std::string::npos ? source.size() : lineEnd + 1;
  return source.substr(0, lineStart) + includeText + "\n" + source.substr(lineEnd);
}

std::string ExpandUnrollPragmas(const std::string& source) {
  constexpr const char* marker = "#pragma tgfx_spec_unroll";
  const size_t markerLength = std::char_traits<char>::length(marker);
  const size_t maxSites =
      std::max(ChainResourceCapacity.instructions, NoiseGraphResourceCapacity.instructions);
  const auto trim = [](const std::string& text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
      return std::string();
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
  };
  std::string result;
  size_t lineStart = 0;
  while (lineStart <= source.size()) {
    auto lineEnd = source.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
      lineEnd = source.size();
    }
    const std::string line = source.substr(lineStart, lineEnd - lineStart);
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos || line.compare(first, markerLength, marker) != 0) {
      result += line;
    } else {
      const std::string rest = line.substr(first + markerLength);
      const auto open = rest.find('(');
      const auto close = rest.rfind(')');
      if (open == std::string::npos || close == std::string::npos || close < open) {
        return {};
      }
      std::istringstream head(rest.substr(0, open));
      std::string functionName;
      std::string capacity;
      std::string guard;
      head >> functionName >> capacity >> guard;
      if (functionName.empty() || capacity.empty() || guard.empty()) {
        return {};
      }
      const auto args = trim(rest.substr(open + 1, close - open - 1));
      for (size_t n = 0; n < maxSites; ++n) {
        const auto index = std::to_string(n);
        result += "#if " + capacity + " > " + index + "\n";
        result += "    if (" + guard + "() > " + index + ") { " + functionName + "(" + index +
                  (args.empty() ? "" : ", " + args) + "); }\n";
        result += "#endif\n";
      }
    }
    if (lineEnd < source.size()) {
      result += '\n';
    }
    lineStart = lineEnd + 1;
  }
  return result;
}

}  // namespace tgfx
