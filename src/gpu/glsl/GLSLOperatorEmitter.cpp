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

const char* GLSLTypeName(SLType type) {
  switch (type) {
    case SLType::Float:
      return "float";
    case SLType::Float2:
      return "vec2";
    case SLType::Float3:
      return "vec3";
    case SLType::Float4:
      return "vec4";
    case SLType::Int:
      return "int";
    case SLType::Int2:
      return "ivec2";
    case SLType::Int3:
      return "ivec3";
    case SLType::Int4:
      return "ivec4";
    case SLType::Float2x2:
      return "mat2";
    case SLType::Float3x3:
      return "mat3";
    case SLType::Float4x4:
      return "mat4";
    default:
      return nullptr;
  }
}

bool ValueName(const ProgramIR& program, ValueID id, std::string* name, std::string* error);
bool EmitExpression(const ProgramIR& program, ValueID id, std::string* expression,
                    std::string* error);

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
  if (!value.debugName.empty()) {
    *name = value.debugName;
    return true;
  }
  if (value.kind == IRValueKind::Expr || value.kind == IRValueKind::Field) {
    return EmitExpression(program, id, name, error);
  }
  if (error != nullptr) {
    *error = "an operator argument has no printable name (id=" + std::to_string(id) +
             ", type=" + std::to_string(static_cast<int>(value.type)) +
             ", kind=" + std::to_string(static_cast<int>(value.kind)) + ")";
  }
  return false;
}

bool EmitExpression(const ProgramIR& program, ValueID id, std::string* expression,
                    std::string* error) {
  const auto& value = program.values[id];
  if (value.definingExpr == INVALID_EXPR_ID || value.definingExpr >= program.exprs.size()) {
    return Fail(error, "an operator argument expression has no definition");
  }
  const auto& expr = program.exprs[value.definingExpr];
  switch (expr.kind) {
    case IRExprKind::Swizzle: {
      std::string source = {};
      if (!GLSLOperatorEmitter::EmitValue(program, expr.args[0], &source, error)) {
        return false;
      }
      *expression = source + "." + expr.swizzle;
      return true;
    }
    case IRExprKind::Construct: {
      const char* typeName = GLSLTypeName(expr.type);
      if (typeName == nullptr) {
        return Fail(error, "a construct expression has no GLSL type");
      }
      std::vector<std::string> args(expr.args.size());
      bool sameArgument = true;
      for (size_t index = 0; index < expr.args.size(); ++index) {
        if (!GLSLOperatorEmitter::EmitValue(program, expr.args[index], &args[index], error)) {
          return false;
        }
        if (index != 0 && expr.args[index] != expr.args[0]) {
          sameArgument = false;
        }
      }
      std::string out = typeName;
      out += "(";
      if (expr.args.empty()) {
        return Fail(error, "a construct expression has no arguments");
      }
      if (sameArgument) {
        out += args[0];
      } else {
        for (size_t index = 0; index < args.size(); ++index) {
          if (index != 0) {
            out += ", ";
          }
          out += args[index];
        }
      }
      out += ")";
      *expression = std::move(out);
      return true;
    }
    case IRExprKind::Convert: {
      if (expr.args.size() != 1) {
        return Fail(error, "a conversion needs one source");
      }
      std::string source = {};
      if (!GLSLOperatorEmitter::EmitValue(program, expr.args[0], &source, error)) {
        return false;
      }
      *expression = (expr.type == SLType::Float ? "float(" : "int(") + source + ")";
      return true;
    }
    case IRExprKind::Binary: {
      std::string left = {};
      std::string right = {};
      if (!GLSLOperatorEmitter::EmitValue(program, expr.args[0], &left, error) ||
          !GLSLOperatorEmitter::EmitValue(program, expr.args[1], &right, error)) {
        return false;
      }
      if (expr.binaryOp == IRBinaryOp::Min || expr.binaryOp == IRBinaryOp::Max) {
        *expression = std::string(expr.binaryOp == IRBinaryOp::Min ? "min(" : "max(") + left +
                      ", " + right + ")";
        return true;
      }
      const char* op = nullptr;
      switch (expr.binaryOp) {
        case IRBinaryOp::Add:
          op = "+";
          break;
        case IRBinaryOp::Sub:
          op = "-";
          break;
        case IRBinaryOp::Mul:
          op = "*";
          break;
        case IRBinaryOp::Div:
          op = "/";
          break;
        case IRBinaryOp::Min:
        case IRBinaryOp::Max:
          break;
      }
      *expression = "(" + left + " " + op + " " + right + ")";
      return true;
    }
    case IRExprKind::Call:
      return GLSLOperatorEmitter::EmitCall(program, id, expression, error);
    case IRExprKind::TextureSample: {
      if (expr.args.size() != 2) {
        return Fail(error, "a texture sample needs a sampler and a coordinate");
      }
      std::string sampler = {};
      std::string coord = {};
      if (!ValueName(program, expr.args[0], &sampler, error) ||
          !GLSLOperatorEmitter::EmitValue(program, expr.args[1], &coord, error)) {
        return false;
      }
      if (expr.samplerType == SLType::Texture2DRectSampler) {
        // The rect reader takes the sampler and coordinate alone (no subset clamp, no swizzle).
        *expression = "tgfxTextureSampleRect(" + sampler + ", " + coord + ")";
        return true;
      }
      // A plain sample (no swizzle code recorded) reads the sampler's own channels, which the
      // shared reader expresses with the identity swizzle code. A clamped sample carries the
      // subset uniform (the stricter of the strict/draw pair rides the call) with clamp on.
      const int32_t swizzleCode = expr.samplerSwizzleCode < 0 ? 0 : expr.samplerSwizzleCode;
      std::string subset = "vec4(0.0)";
      std::string clampOn = "0";
      if (expr.subsetClamped) {
        const ValueID subsetValue = expr.strictSubset != INVALID_VALUE_ID ? expr.strictSubset
                                                                          : expr.drawSubset;
        if (subsetValue != INVALID_VALUE_ID && subsetValue < program.values.size()) {
          std::string subsetName = {};
          if (ValueName(program, subsetValue, &subsetName, error)) {
            subset = subsetName;
            clampOn = "1";
          }
        }
      }
      *expression = "tgfxTextureSample(" + sampler + ", " + coord + ", " + subset + ", " +
                    clampOn + ", " + std::to_string(swizzleCode) + ")";
      return true;
    }
    case IRExprKind::Builtin:
      *expression = expr.builtin;
      return true;
    case IRExprKind::ConstantFloat:
    case IRExprKind::ConstantInt:
      return Fail(error, "a constant must be named through its value");
    default:
      break;
  }
  return Fail(error, "an operator argument expression cannot be emitted as GLSL");
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
    // An argument with a defining expression prints the expression inline: the IR printer route
    // has no named staging local for it, and a bare debug name would be undeclared there. Resource
    // and named values keep their names, which the text route declares.
    if (!EmitValue(program, call.args[index], &argument, error)) {
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

bool GLSLOperatorEmitter::EmitValue(const ProgramIR& program, ValueID value,
                                    std::string* expression, std::string* error) {
  if (value == INVALID_VALUE_ID || value >= program.values.size()) {
    return Fail(error, "the value has an invalid id");
  }
  const auto kind = program.values[value].kind;
  if (kind == IRValueKind::Expr || kind == IRValueKind::Field) {
    return EmitExpression(program, value, expression, error);
  }
  return ValueName(program, value, expression, error);
}

}  // namespace tgfx
