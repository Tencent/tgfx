/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLOperatorEmitter.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tgfx {
namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

std::string FormatFloat(float value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.9g", value);
  std::string text = buffer;
  if (text.find_first_of(".eE") == std::string::npos) {
    text += ".0";
  }
  return text;
}

bool ValueName(const ProgramIR& program, ValueID id, std::string* name, std::string* error) {
  if (id == INVALID_VALUE_ID || id >= program.values.size()) {
    return Fail(error, "an operator argument has an invalid value");
  }
  const auto& value = program.values[id];
  if (value.kind == IRValueKind::Constant) {
    if (value.definingExpr >= program.exprs.size()) {
      return Fail(error, "a constant has no defining expression");
    }
    const auto& constant = program.exprs[value.definingExpr];
    if (constant.kind == IRExprKind::ConstantInt) {
      *name = std::to_string(static_cast<int32_t>(constant.constantBits));
      return true;
    }
    if (constant.kind != IRExprKind::ConstantFloat) {
      return Fail(error, "a constant has an unsupported expression");
    }
    float valueAsFloat = 0.0f;
    static_assert(sizeof(valueAsFloat) == sizeof(constant.constantBits));
    std::memcpy(&valueAsFloat, &constant.constantBits, sizeof(valueAsFloat));
    if (!std::isfinite(valueAsFloat)) {
      return Fail(error, "a non-finite float cannot be emitted as GLSL");
    }
    *name = FormatFloat(valueAsFloat);
    return true;
  }
  if (value.debugName.empty()) {
    return Fail(error, "an operator argument has no printable name");
  }
  *name = value.debugName;
  return true;
}

}  // namespace

bool GLSLOperatorEmitter::EmitCall(const ProgramIR& program, ValueID result,
                                   std::string* expression, std::string* error) {
  if (expression == nullptr) {
    return Fail(error, "the output expression is null");
  }
  if (result == INVALID_VALUE_ID || result >= program.values.size()) {
    return Fail(error, "the call result has an invalid value");
  }
  const auto& value = program.values[result];
  if (value.definingExpr == INVALID_EXPR_ID || value.definingExpr >= program.exprs.size()) {
    return Fail(error, "the call result has no defining expression");
  }
  const auto& call = program.exprs[value.definingExpr];
  if (call.kind != IRExprKind::Call) {
    return Fail(error, "the value is not defined by an operator call");
  }
  const auto* desc = FindOperator(call.op);
  if (desc == nullptr || desc->glslSymbol == nullptr) {
    return Fail(error, "the operator has no GLSL symbol");
  }
  if (call.args.size() != desc->params.size()) {
    return Fail(error, "the operator argument count does not match the manifest");
  }

  std::string out = desc->glslSymbol;
  out += "(";
  for (size_t index = 0; index < call.args.size(); ++index) {
    std::string argument = {};
    if (!ValueName(program, call.args[index], &argument, error)) {
      return false;
    }
    if (index != 0) {
      out += ", ";
    }
    out += argument;
  }
  out += ")";
  *expression = std::move(out);
  return true;
}

}  // namespace tgfx
