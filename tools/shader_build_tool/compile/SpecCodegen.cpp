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
#include "gpu/shaders/contract/SpecContract.h"
#include <cctype>
#include <map>
#include "core/DiagnosticCodes.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/Uniform.h"

namespace tgfx {

namespace {

constexpr const char* kSpecMarker = "#pragma tgfx_spec_include";
constexpr const char* kSpecCallPrefix = SpecContract::FieldAccessorPrefix;
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
  static const std::string kGatePragma = "#pragma tgfx_spec_gate ";
  for (size_t at = stripped.find(kGatePragma); at != std::string::npos;
       at = stripped.find(kGatePragma, at + 1)) {
    // #pragma tgfx_spec_gate <NAME> : <expr> — the syntax itself is validated by the source
    // rules (ValidateSpecGates); the scan only collects well-formed entries.
    auto cursor = at + kGatePragma.size();
    auto nameEnd = cursor;
    while (nameEnd < stripped.size() && IsIdentifierChar(stripped[nameEnd])) {
      ++nameEnd;
    }
    auto colon = stripped.find(':', nameEnd);
    auto lineEnd = stripped.find('\n', nameEnd);
    if (nameEnd > cursor && colon != std::string::npos && colon < lineEnd) {
      auto exprStart = colon + 1;
      while (exprStart < stripped.size() && (stripped[exprStart] == ' ' || stripped[exprStart] == '\t')) {
        ++exprStart;
      }
      auto exprEnd = lineEnd;
      while (exprEnd > exprStart && (stripped[exprEnd - 1] == ' ' || stripped[exprEnd - 1] == '\t' ||
                                     stripped[exprEnd - 1] == '\r')) {
        --exprEnd;
      }
      const auto name = stripped.substr(cursor, nameEnd - cursor);
      bool duplicate = false;
      for (const auto& gate : usage.gates) {
        if (gate.name == name) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate && exprEnd > exprStart) {
        // The gated function: the next '(' after the pragma line, with its name the identifier
        // ending there. Parsed here because the pragma itself does not survive preprocessing.
        auto paren = stripped.find('(', lineEnd);
        auto nameEnd2 = paren;
        while (nameEnd2 > lineEnd && !IsIdentifierChar(stripped[nameEnd2 - 1])) {
          --nameEnd2;
        }
        auto nameStart2 = nameEnd2;
        while (nameStart2 > lineEnd && IsIdentifierChar(stripped[nameStart2 - 1])) {
          --nameStart2;
        }
        if (paren == std::string::npos || nameStart2 == nameEnd2) {
          ReportSpecDiagnostic(diagnostics, DiagnosticCode::SpecGateUnmatched, shaderName, 0,
                               "gate '" + name + "' is not followed by a function definition");
        } else {
          usage.gates.push_back(SpecGateInfo{name, stripped.substr(exprStart, exprEnd - exprStart),
                                             stripped.substr(nameStart2, nameEnd2 - nameStart2)});
        }
      }
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
    // SPEC_GATE_<name>() is a gate's guard call, not a field read: skipping it here keeps it
    // out of the field list (a field named GATE_... would poison the id assignment and the
    // stub macros with a conflicting define).
    if (name.rfind("GATE_", 0) == 0) {
      i = nameEnd;
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

std::string GenerateSpecInclude(const std::vector<SpecEntry>& entries, bool specDialect,
                                const std::vector<SpecGateInfo>& gates) {
  std::string text;
  text += "// Generated by shader_build_tool (compile/SpecCodegen); do not edit.\n";
  text += "// The master switch first: 0 (the default) keeps the generic behavior reading the\n";
  text += "// uniforms; the runtime injects 1 plus the field values per draw shape.\n";
  text += std::string("#define ") + SpecContract::UnrolledExecutorMacro + " 1\n";
  if (specDialect) {
    text += std::string("layout(constant_id = 0) const int ") + SpecContract::MasterSwitch +
            " = 0;\n";
  } else {
    text += std::string("const int ") + SpecContract::MasterSwitch + " = 0;\n";
  }
  for (const auto& entry : entries) {
    for (uint32_t id = entry.firstId;
         id < entry.firstId + static_cast<uint32_t>(entry.arraySize) * (entry.arraySize > 1 ? 4u : 1u);
         ++id) {
      if (specDialect) {
        text += std::string("layout(constant_id = ") + std::to_string(id) + ") const int " +
                SpecContract::ConstantPrefix + std::to_string(id) + " = 0;\n";
      } else {
        text += std::string("const int ") + SpecContract::ConstantPrefix + std::to_string(id) +
                " = 0;\n";
      }
    }
  }
  text += std::string("bool ") + SpecContract::ActiveAccessor + "() { int s = " +
          SpecContract::MasterSwitch + "; return s == 1; }\n";
  for (const auto& entry : entries) {
    if (entry.arraySize == 1) {
      text += std::string("int ") + SpecContract::FieldAccessorPrefix + entry.name +
              "() { int s = " + SpecContract::MasterSwitch + "; if (s == 1) { return " +
              SpecContract::ConstantPrefix + std::to_string(entry.firstId) + "; } return " +
              entry.name + "; }\n";
      continue;
    }
    text += std::string("ivec4 ") + SpecContract::FieldAccessorPrefix + entry.name + "(int i) {\n";
    text += std::string("  int s = ") + SpecContract::MasterSwitch + ";\n";
    text += "  if (s == 1) {\n";
    for (uint32_t element = 0; element < entry.arraySize; ++element) {
      auto base = entry.firstId + element * 4u;
      text += std::string("    if (i == ") + std::to_string(element) + ") { int a = " +
              SpecContract::ConstantPrefix + std::to_string(base) + "; int b = " +
              SpecContract::ConstantPrefix + std::to_string(base + 1) + "; int c = " +
              SpecContract::ConstantPrefix + std::to_string(base + 2) + "; int d = " +
              SpecContract::ConstantPrefix + std::to_string(base + 3) +
              "; return ivec4(a, b, c, d); }\n";
    }
    text += "    return ivec4(" + std::to_string(Op::None) + ", -2, -2, 0);\n";
    text += "  }\n";
    text += "  return " + entry.name + "[i];\n";
    text += "}\n";
  }
  // The gate guards: one bool accessor per gate, identical in both dialects. Generic (the master
  // switch is 0) every gate is open; specialized the driver folds the expression with the
  // constant values, so an unguarded call of a closed gate is dead code the optimizer removes.
  for (const auto& gate : gates) {
    text += std::string("bool ") + SpecContract::GateAccessorPrefix + gate.name + "() { return !" +
            SpecContract::ActiveAccessor + "() || (" + gate.expr + "); }\n";
  }
  return text;
}

std::string GenerateSpecStubInclude(const SpecUsage& usage) {
  std::string text;
  text += "// Generated by shader_build_tool (compile/SpecCodegen); the stub pass's pass-throughs.\n";
  text += std::string("#define ") + SpecContract::ActiveAccessor + "() false\n";
  // The stub exists for the reflection and the inline policy, so it compiles the loop form
  // alone: the unrolled executor is the specialization machinery the real forms carry.
  text += std::string("#define ") + SpecContract::UnrolledExecutorMacro + " 0\n";
  for (const auto& field : usage.fields) {
    if (field.indexed) {
      text += std::string("#define ") + SpecContract::FieldAccessorPrefix + field.name + "(i) " +
              field.name + "[i]\n";
    } else {
      text += std::string("#define ") + SpecContract::FieldAccessorPrefix + field.name + "() " +
              field.name + "\n";
    }
  }
  for (const auto& gate : usage.gates) {
    text += std::string("#define ") + SpecContract::GateAccessorPrefix + gate.name + "() true\n";
  }
  return text;
}

namespace {

// One gated function's span in a text whose gate pragma was already located (and erased).
struct GatedFunctionSpan {
  size_t bodyStart = 0;
  size_t bodyEnd = 0;
  std::string returnType;
};

// The zero-value literal a closed gate's stub returns. Empty when the return type cannot be
// stubbed (structs and matrices): such a function cannot be gated.
std::string StubReturnValue(const std::string& rawType) {
  std::string type = rawType;
  for (const auto* qualifier : {"highp ", "mediump ", "lowp ", "const "}) {
    const auto at = type.find(qualifier);
    if (at == 0) {
      type.erase(0, std::char_traits<char>::length(qualifier));
    }
  }
  while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) {
    type.pop_back();
  }
  if (type == "vec4" || type == "vec3" || type == "vec2") {
    return type + "(0.0)";
  }
  if (type == "float") {
    return "0.0";
  }
  if (type == "int" || type == "uint") {
    return "0";
  }
  if (type == "bool") {
    return "false";
  }
  return {};
}

// Locates the definition of functionName: an occurrence of " name(" whose line prefix is a
// plain return type (call sites carry operators or the return keyword and never match), then
// the matching ')' and the brace-matched body. The return type is the prefix itself.
bool FindGatedFunction(const std::string& text, const std::string& functionName,
                       GatedFunctionSpan* span, std::string* error) {
  static const char* kReturnTypes[] = {"bool",  "int",    "float", "vec2", "vec3",
                                       "vec4",  "mat3",   "mat4",  "void", "highp vec2",
                                       "highp vec4"};
  const std::string needle = " " + functionName + "(";
  auto at = text.find(needle);
  bool found = false;
  size_t sigStart = 0;
  size_t parenOpen = 0;
  std::string foundPrefix;
  while (at != std::string::npos) {
    const auto lineStart = text.rfind('\n', at);
    sigStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    std::string prefix = text.substr(sigStart, at - sigStart);
    while (!prefix.empty() && (prefix.back() == ' ' || prefix.back() == '\t')) {
      prefix.pop_back();
    }
    for (const auto* type : kReturnTypes) {
      if (prefix == type) {
        found = true;
        break;
      }
    }
    if (found) {
      foundPrefix = prefix;
      parenOpen = at + needle.size() - 1;
      break;
    }
    at = text.find(needle, at + 1);
  }
  if (!found) {
    *error = "the gated function '" + functionName + "' has no definition in this text";
    return false;
  }
  int depth = 0;
  auto i = parenOpen;
  for (; i < text.size(); ++i) {
    if (text[i] == '(') {
      ++depth;
    } else if (text[i] == ')' && --depth == 0) {
      break;
    }
  }
  if (depth != 0) {
    *error = "unbalanced parameter list of the gated function '" + functionName + "'";
    return false;
  }
  auto bodyStart = text.find('{', i);
  if (bodyStart == std::string::npos) {
    *error = "the gated function has no body";
    return false;
  }
  int braceDepth = 0;
  auto j = bodyStart;
  for (; j < text.size(); ++j) {
    if (text[j] == '{') {
      ++braceDepth;
    } else if (text[j] == '}' && --braceDepth == 0) {
      break;
    }
  }
  if (braceDepth != 0) {
    *error = "unbalanced body of the gated function";
    return false;
  }
  span->bodyStart = bodyStart;
  span->bodyEnd = j;
  span->returnType = std::move(foundPrefix);
  return true;
}

// The OP_<NAME> constants, from the one table every compiled variant also receives.
const std::map<std::string, std::string>& OpConstantMap() {
  static const std::map<std::string, std::string> map = [] {
    std::map<std::string, std::string> result;
    for (const auto& define : Op::Defines()) {
      const auto eq = define.find('=');
      if (eq != std::string::npos) {
        result.emplace(define.substr(0, eq), define.substr(eq + 1));
      }
    }
    return result;
  }();
  return map;
}

// Translates a gate expression into a preprocessor condition over the value macros: every
// SPEC_<Name>() becomes TGFX_SPEC_<id>, every OP_<NAME> its numeric value.
bool TranslateGateExpression(const std::string& expr, const std::vector<SpecEntry>& entries,
                             std::string* condition, std::string* error) {
  std::string out;
  size_t i = 0;
  while (i < expr.size()) {
    if (IsIdentifierChar(expr[i])) {
      auto end = i;
      while (end < expr.size() && IsIdentifierChar(expr[end])) {
        ++end;
      }
      const auto token = expr.substr(i, end - i);
      if (token.compare(0, 5, "SPEC_") == 0) {
        const auto field = token.substr(5);
        const SpecEntry* found = nullptr;
        for (const auto& entry : entries) {
          if (entry.name == field && entry.arraySize == 1) {
            found = &entry;
            break;
          }
        }
        if (found == nullptr) {
          *error = "gate expression reads SPEC_" + field + "(), which this variant's spec table " +
                   "does not carry as a scalar";
          return false;
        }
        auto after = end;
        while (after < expr.size() && (expr[after] == ' ' || expr[after] == '\t')) {
          ++after;
        }
        if (after >= expr.size() || expr[after] != '(' || after + 1 >= expr.size() ||
            expr[after + 1] != ')') {
          *error = "gate expression uses the indexed SPEC_" + field + "(i) form; only scalars";
          return false;
        }
        out += std::string(SpecContract::ConstantPrefix) + std::to_string(found->firstId);
        end = after + 2;
      } else if (token.compare(0, 3, "OP_") == 0) {
        const auto it = OpConstantMap().find(token);
        if (it == OpConstantMap().end()) {
          *error = "gate expression references unknown constant " + token;
          return false;
        }
        out += it->second;
      } else if (token.find_first_not_of("0123456789") == std::string::npos) {
        out += token;  // an integer literal passes through
      } else {
        *error = "gate expression references unknown identifier " + token +
                 " (allowed: SPEC_<field>(), OP_<OP> constants, integer literals)";
        return false;
      }
      i = end;
      continue;
    }
    out += expr[i];
    ++i;
  }
  *condition = std::move(out);
  return true;
}

}  // namespace

TextGateResult ApplyTextSpecGates(const std::string& preprocessedText,
                                  const std::vector<SpecGateInfo>& gates,
                                  const std::vector<SpecEntry>& entries) {
  TextGateResult result;
  result.text = preprocessedText;
  if (gates.empty()) {
    return result;
  }
  std::string& text = result.text;
  std::string header;
  for (const auto& gate : gates) {
    // The pragma does not survive preprocessing (the preprocessor drops unknown pragma lines),
    // so the pass locates the gated function by name. A variant that pruned the gated file
    // simply does not carry the definition and the gate does not apply.
    GatedFunctionSpan span;
    std::string spanError;
    if (!FindGatedFunction(text, gate.functionName, &span, &spanError)) {
      continue;
    }
    const auto stub = StubReturnValue(span.returnType);
    if (stub.empty()) {
      result.error = "gate '" + gate.name + "' wraps a function returning '" + span.returnType +
                     "', which has no zero-value stub; gate a function with a scalar or vector " +
                     "return type";
      return result;
    }
    std::string condition;
    std::string conditionError;
    if (!TranslateGateExpression(gate.expr, entries, &condition, &conditionError)) {
      result.error = "gate '" + gate.name + "': " + conditionError;
      return result;
    }
    header += std::string("#define ") + SpecContract::GateMacroPrefix + gate.name + " (" +
              SpecContract::MasterSwitch + " == 0 || (" + condition + "))\n";
    const auto body = text.substr(span.bodyStart, span.bodyEnd - span.bodyStart + 1);
    // The wrapper opens with a newline: the body's opening brace may share the signature's line
    // (K&R style), and a preprocessor directive must start its own line.
    const auto replacement = std::string("\n#if ") + SpecContract::GateMacroPrefix + gate.name +
                             "\n" + body + "\n#else\n{ return " +
                             stub + "; }\n#endif\n";
    text.replace(span.bodyStart, span.bodyEnd - span.bodyStart + 1, replacement);
    ++result.wrapped;
  }
  if (result.wrapped == 0) {
    return result;
  }
  const auto versionPos = text.find("#version");
  if (versionPos == std::string::npos) {
    result.error = "the gated text has no #version line to hang the gate defines on";
    return result;
  }
  const auto versionEnd = text.find('\n', versionPos);
  text.insert(versionEnd == std::string::npos ? text.size() : versionEnd + 1, header);
  return result;
}

std::string StubOutGatedFunctions(const std::string& source, const std::vector<SpecGateInfo>& gates) {
  std::string text = source;
  for (const auto& gate : gates) {
    GatedFunctionSpan span;
    std::string spanError;
    if (!FindGatedFunction(text, gate.functionName, &span, &spanError)) {
      continue;  // this variant pruned the gated file
    }
    const auto stub = StubReturnValue(span.returnType);
    if (stub.empty()) {
      return {};
    }
    text.replace(span.bodyStart, span.bodyEnd - span.bodyStart + 1, "{ return " + stub + "; }\n");
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
