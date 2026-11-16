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
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include "core/DiagnosticCodes.h"
#include "core/Diagnostics.h"
#include "gpu/Uniform.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/contract/SpecContract.h"

namespace tgfx {

namespace {

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

/// Reports one gate-scan diagnostic against the family the scan ran on. A malformed gate
/// declaration is a build error, never a silent skip: a gate that never applies would leave the
/// specialized text carrying code every draw pays for.
void ReportGateDiagnostic(Diagnostics* diagnostics, const char* code, const std::string& shaderName,
                          const std::string& message) {
  if (diagnostics == nullptr) {
    return;
  }
  Diagnostic diagnostic;
  diagnostic.severity = Severity::Error;
  diagnostic.layer = Layer::Compile;
  diagnostic.code = code;
  diagnostic.shader = shaderName;
  diagnostic.message = message;
  diagnostics->report(std::move(diagnostic));
}

}  // namespace

namespace {

// One gated function's span in a text whose gate pragma was already located (and erased).
struct GatedFunctionSpan {
  size_t bodyStart = 0;
  size_t bodyEnd = 0;
  std::string returnType;
};

// The zero-value literal a closed gate's stub returns. Empty when the return type cannot be
// stubbed (structs and matrices): such a function cannot be gated.
std::string StubReturnValue(const std::string& rawType, bool hlsl) {
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
  if (hlsl) {
    if (type == "float4") {
      return "float4(0.0, 0.0, 0.0, 0.0)";
    }
    if (type == "float3") {
      return "float3(0.0, 0.0, 0.0)";
    }
    if (type == "float2") {
      return "float2(0.0, 0.0)";
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
// the matching ')' and the brace-matched body. The return type is the qualifier-stripped prefix.
bool FindGatedFunction(const std::string& text, const std::string& functionName, bool hlsl,
                       GatedFunctionSpan* span, std::string* error) {
  static const char* kGlslReturnTypes[] = {"bool", "int",  "uint", "float", "vec2",
                                           "vec3", "vec4", "mat3", "mat4",  "void"};
  static const char* kHlslReturnTypes[] = {"bool",   "int",    "uint",     "float",    "float2",
                                           "float3", "float4", "float3x3", "float4x4", "void"};
  const char* const* returnTypes = hlsl ? kHlslReturnTypes : kGlslReturnTypes;
  const size_t returnTypeCount = hlsl ? sizeof(kHlslReturnTypes) / sizeof(kHlslReturnTypes[0])
                                      : sizeof(kGlslReturnTypes) / sizeof(kGlslReturnTypes[0]);
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
    // A translator may qualify a definition (static, inline, precision); the gate only needs the
    // type itself, and the stub writer strips the same qualifiers again.
    for (const auto* qualifier : {"static ", "inline ", "highp ", "mediump ", "lowp "}) {
      const auto atQualifier = prefix.find(qualifier);
      if (atQualifier == 0) {
        prefix.erase(0, std::char_traits<char>::length(qualifier));
      }
    }
    for (size_t index = 0; index < returnTypeCount; ++index) {
      if (prefix == returnTypes[index]) {
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
// SPEC_<Name>() becomes <constantPrefix><id>, every OP_<NAME> its numeric value.
bool TranslateGateExpression(const std::string& expr, const std::vector<SpecEntry>& entries,
                             const char* constantPrefix, std::string* condition,
                             std::string* error) {
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
        out += std::string(constantPrefix) + std::to_string(found->firstId);
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
                                  const std::vector<SpecEntry>& entries,
                                  const GateDialect& dialect) {
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
    if (!FindGatedFunction(text, gate.functionName, dialect.hlsl, &span, &spanError)) {
      continue;
    }
    const auto stub = StubReturnValue(span.returnType, dialect.hlsl);
    if (stub.empty()) {
      result.error = "gate '" + gate.name + "' wraps a function returning '" + span.returnType +
                     "', which has no zero-value stub; gate a function with a scalar or vector " +
                     "return type";
      return result;
    }
    std::string condition;
    std::string conditionError;
    if (!TranslateGateExpression(gate.expr, entries, dialect.constantPrefix, &condition,
                                 &conditionError)) {
      result.error = "gate '" + gate.name + "': " + conditionError;
      return result;
    }
    header += std::string("#define ") + SpecContract::GateMacroPrefix + gate.name + " (" +
              std::string(dialect.constantPrefix) + "0 == 0 || (" + condition + "))\n";
    const auto body = text.substr(span.bodyStart, span.bodyEnd - span.bodyStart + 1);
    // The wrapper opens with a newline: the body's opening brace may share the signature's line
    // (K&R style), and a preprocessor directive must start its own line.
    const auto replacement = std::string("\n#if ") + SpecContract::GateMacroPrefix + gate.name +
                             "\n" + body + "\n#else\n{ return " + stub + "; }\n#endif\n";
    text.replace(span.bodyStart, span.bodyEnd - span.bodyStart + 1, replacement);
    ++result.wrapped;
  }
  if (result.wrapped == 0) {
    return result;
  }
  if (dialect.hlsl) {
    // The SPIRV-Cross HLSL opens with includes, not a version directive; the defines go first so
    // every later #if sees them, and the value macros themselves arrive later (their #ifndef
    // defaults sit under the includes), which the preprocessor resolves at the #if site.
    text.insert(0, header);
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

std::string StubOutGatedFunctions(const std::string& source,
                                  const std::vector<SpecGateInfo>& gates) {
  std::string text = source;
  for (const auto& gate : gates) {
    GatedFunctionSpan span;
    std::string spanError;
    if (!FindGatedFunction(text, gate.functionName, false, &span, &spanError)) {
      continue;  // this variant pruned the gated file
    }
    const auto stub = StubReturnValue(span.returnType, false);
    if (stub.empty()) {
      return {};
    }
    text.replace(span.bodyStart, span.bodyEnd - span.bodyStart + 1, "{ return " + stub + "; }\n");
  }
  return text;
}

std::vector<SpecGateInfo> ScanGatePragmas(const std::string& fragmentSource,
                                          const std::string& shaderName, Diagnostics* diagnostics) {
  std::vector<SpecGateInfo> gates;
  const auto stripped = StripComments(fragmentSource);
  static const std::string kGatePragma = "#pragma tgfx_spec_gate ";
  for (size_t at = stripped.find(kGatePragma); at != std::string::npos;
       at = stripped.find(kGatePragma, at + 1)) {
    // #pragma tgfx_spec_gate <NAME> : <expr> — the syntax itself is validated by the source
    // rules; the scan only collects well-formed entries.
    auto cursor = at + kGatePragma.size();
    auto nameEnd = cursor;
    while (nameEnd < stripped.size() && IsIdentifierChar(stripped[nameEnd])) {
      ++nameEnd;
    }
    auto colon = stripped.find(':', nameEnd);
    auto lineEnd = stripped.find('\n', nameEnd);
    if (nameEnd > cursor && colon != std::string::npos &&
        (lineEnd == std::string::npos || colon < lineEnd)) {
      auto exprStart = colon + 1;
      while (exprStart < stripped.size() &&
             (stripped[exprStart] == ' ' || stripped[exprStart] == '\t')) {
        ++exprStart;
      }
      auto exprEnd = lineEnd == std::string::npos ? stripped.size() : lineEnd;
      while (exprEnd > exprStart &&
             (stripped[exprEnd - 1] == ' ' || stripped[exprEnd - 1] == '\t' ||
              stripped[exprEnd - 1] == '\r')) {
        --exprEnd;
      }
      const auto name = stripped.substr(cursor, nameEnd - cursor);
      bool duplicate = false;
      for (const auto& gate : gates) {
        if (gate.name == name) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate && exprEnd > exprStart) {
        // The gated function: the next '(' after the pragma line, with its name the identifier
        // ending there. Parsed here because the pragma itself does not survive preprocessing.
        const auto paren = stripped.find('(', lineEnd);
        if (paren == std::string::npos) {
          ReportGateDiagnostic(diagnostics, DiagnosticCode::SpecGateUnmatched, shaderName,
                               "gate '" + name + "' is not followed by a function definition");
          continue;
        }
        auto nameEnd2 = paren;
        while (nameEnd2 > lineEnd + 1 && !IsIdentifierChar(stripped[nameEnd2 - 1])) {
          --nameEnd2;
        }
        auto nameStart2 = nameEnd2;
        while (nameStart2 > lineEnd + 1 && IsIdentifierChar(stripped[nameStart2 - 1])) {
          --nameStart2;
        }
        if (nameStart2 == nameEnd2) {
          ReportGateDiagnostic(diagnostics, DiagnosticCode::SpecGateUnmatched, shaderName,
                               "gate '" + name + "' is not followed by a function definition");
        } else {
          gates.push_back(SpecGateInfo{name, stripped.substr(exprStart, exprEnd - exprStart),
                                       stripped.substr(nameStart2, nameEnd2 - nameStart2)});
        }
      }
    }
  }
  return gates;
}

bool GateEvaluatable(const SpecGateInfo& gate, const std::vector<SpecEntry>& entries) {
  size_t at = 0;
  while ((at = gate.expr.find(SpecContract::FieldAccessorPrefix, at)) != std::string::npos) {
    const bool boundary =
        at == 0 ||
        !(std::isalnum(static_cast<unsigned char>(gate.expr[at - 1])) || gate.expr[at - 1] == '_');
    size_t end = at + std::char_traits<char>::length(SpecContract::FieldAccessorPrefix);
    while (end < gate.expr.size() &&
           (std::isalnum(static_cast<unsigned char>(gate.expr[end])) || gate.expr[end] == '_')) {
      ++end;
    }
    if (boundary && end > at + std::char_traits<char>::length(SpecContract::FieldAccessorPrefix)) {
      const auto field = gate.expr.substr(
          at + std::char_traits<char>::length(SpecContract::FieldAccessorPrefix),
          end - at - std::char_traits<char>::length(SpecContract::FieldAccessorPrefix));
      const auto found = std::find_if(
          entries.begin(), entries.end(),
          [&field](const SpecEntry& entry) { return entry.name == field && entry.arraySize == 1; });
      if (found == entries.end()) {
        return false;
      }
    }
    at = end;
  }
  return true;
}

}  // namespace tgfx
