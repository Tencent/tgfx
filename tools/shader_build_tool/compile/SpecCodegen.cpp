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
#include <cctype>
#include <map>
#include <sstream>
#include "core/DiagnosticCodes.h"
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

/// Whether one line of the comment-stripped source declares the uniform `name`: its trimmed form
/// starts with the type keyword and names the member, which is how every UBO declaration in the
/// kernels is spelled (`int SlotCount;`, `ivec4 SlotPacked[CHAIN_SLOT_CAPACITY];`).

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
  static const char* kReturnTypes[] = {"bool", "int",  "float", "vec2",       "vec3",      "vec4",
                                       "mat3", "mat4", "void",  "highp vec2", "highp vec4"};
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
                             "\n" + body + "\n#else\n{ return " + stub + "; }\n#endif\n";
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

std::string StubOutGatedFunctions(const std::string& source,
                                  const std::vector<SpecGateInfo>& gates) {
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

}  // namespace tgfx
