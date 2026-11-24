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

#include "SPIRVShaderBuilder.h"

namespace tgfx {
namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

int SwizzleIndex(char component) {
  switch (component) {
    case 'x':
    case 'r':
      return 0;
    case 'y':
    case 'g':
      return 1;
    case 'z':
    case 'b':
      return 2;
    case 'w':
    case 'a':
      return 3;
    default:
      return -1;
  }
}

bool IsFloatVector(SLType type) {
  return type == SLType::Float || type == SLType::Float2 || type == SLType::Float3 ||
         type == SLType::Float4;
}

}  // namespace

SPIRVShaderBuilder::SPIRVShaderBuilder(ShaderStage stage) : stage(stage) {
}

uint32_t SPIRVShaderBuilder::TypeId(SLType type, std::string* error) {
  const auto key = static_cast<uint32_t>(type);
  const auto found = typeIds.find(key);
  if (found != typeIds.end()) {
    return found->second;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  uint32_t id = 0;
  switch (type) {
    case SLType::Void: {
      id = writer.NewId();
      writer.Inst(spirv::kOpTypeVoid, {id});
      break;
    }
    case SLType::Float: {
      id = writer.NewId();
      writer.Inst(spirv::kOpTypeFloat, {id, 32});
      break;
    }
    case SLType::Int: {
      id = writer.NewId();
      writer.Inst(spirv::kOpTypeInt, {id, 32, 1});
      break;
    }
    case SLType::Float2:
    case SLType::Float3:
    case SLType::Float4: {
      const auto component = TypeId(SLType::Float, error);
      if (component == 0) {
        writer.Sink(saved);
        return 0;
      }
      const uint32_t count = type == SLType::Float2 ? 2 : (type == SLType::Float3 ? 3 : 4);
      id = writer.NewId();
      writer.Inst(spirv::kOpTypeVector, {id, component, count});
      break;
    }
    case SLType::Float2x2:
    case SLType::Float3x3:
    case SLType::Float4x4: {
      const auto column = TypeId(type == SLType::Float2x2
                                     ? SLType::Float2
                                     : (type == SLType::Float3x3 ? SLType::Float3 : SLType::Float4),
                                 error);
      if (column == 0) {
        writer.Sink(saved);
        return 0;
      }
      const uint32_t count = type == SLType::Float2x2 ? 2 : (type == SLType::Float3x3 ? 3 : 4);
      id = writer.NewId();
      writer.Inst(spirv::kOpTypeMatrix, {id, column, count});
      break;
    }
    default:
      writer.Sink(saved);
      Fail(error, "the SPIR-V type is not supported yet");
      return 0;
  }
  writer.Sink(saved);
  typeIds.emplace(key, id);
  return id;
}

uint32_t SPIRVShaderBuilder::PointerTypeId(SLType type, uint32_t storageClass, std::string* error) {
  const auto key = (static_cast<uint64_t>(storageClass) << 32) | static_cast<uint32_t>(type);
  const auto found = pointerTypeIds.find(key);
  if (found != pointerTypeIds.end()) {
    return found->second;
  }
  const auto valueType = TypeId(type, error);
  if (valueType == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto id = writer.NewId();
  writer.Inst(spirv::kOpTypePointer, {id, storageClass, valueType});
  writer.Sink(saved);
  pointerTypeIds.emplace(key, id);
  return id;
}

uint32_t SPIRVShaderBuilder::ConstantId(const IRExpr& expr, std::string* error) {
  const auto key = (static_cast<uint64_t>(expr.type) << 32) | expr.constantBits;
  const auto found = constantIds.find(key);
  if (found != constantIds.end()) {
    return found->second;
  }
  const auto type = TypeId(expr.type, error);
  if (type == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto id = writer.NewId();
  writer.Inst(spirv::kOpConstant, {type, id, expr.constantBits});
  writer.Sink(saved);
  constantIds.emplace(key, id);
  return id;
}

bool SPIRVShaderBuilder::Begin(std::string* error) {
  if (begun) {
    return Fail(error, "the SPIR-V shader builder has already begun");
  }
  if (stage != ShaderStage::Fragment) {
    return Fail(error, "the phase-2 SPIR-V builder supports fragment shaders only");
  }

  writer.Sink(&writer.pre);
  writer.Inst(spirv::kOpCapability, {spirv::kCapabilityShader});
  writer.Inst(spirv::kOpMemoryModel, {0, 1});

  mainId = writer.NewId();
  outputId = writer.NewId();
  writer.Sink(&writer.mode);
  {
    auto operands = std::vector<uint32_t>{spirv::kExecutionModelFragment, mainId};
    const auto name = spirv::NameWords("main");
    operands.insert(operands.end(), name.begin(), name.end());
    operands.push_back(outputId);
    writer.Inst(spirv::kOpEntryPoint, operands);
  }
  writer.Inst(spirv::kOpExecutionMode, {mainId, spirv::kExecutionModeOriginUpperLeft});

  writer.Sink(&writer.debug);
  {
    auto operands = std::vector<uint32_t>{mainId};
    const auto name = spirv::NameWords("main");
    operands.insert(operands.end(), name.begin(), name.end());
    writer.Inst(spirv::kOpName, operands);
  }
  {
    auto operands = std::vector<uint32_t>{outputId};
    const auto name = spirv::NameWords("outputColor");
    operands.insert(operands.end(), name.begin(), name.end());
    writer.Inst(spirv::kOpName, operands);
  }

  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate, {outputId, spirv::kDecorationLocation, 0});

  writer.Sink(&writer.decl);
  const auto voidType = TypeId(SLType::Void, error);
  const auto float4 = TypeId(SLType::Float4, error);
  if (voidType == 0 || float4 == 0) {
    return false;
  }
  const auto outputPointer = PointerTypeId(SLType::Float4, spirv::kStorageOutput, error);
  if (outputPointer == 0) {
    return false;
  }
  writer.Inst(spirv::kOpVariable, {outputPointer, outputId, spirv::kStorageOutput});
  outputPointerId = outputId;
  const auto functionType = writer.NewId();
  writer.Inst(spirv::kOpTypeFunction, {functionType, voidType});

  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpFunction, {voidType, mainId, 0, functionType});
  labelId = writer.NewId();
  writer.Inst(spirv::kOpLabel, {labelId});
  begun = true;
  return true;
}

uint32_t SPIRVShaderBuilder::EmitExpr(const ProgramIR& program, const IRExpr& expr,
                                      std::string* error) {
  switch (expr.kind) {
    case IRExprKind::ConstantFloat:
    case IRExprKind::ConstantInt:
      return ConstantId(expr, error);
    case IRExprKind::Construct: {
      std::vector<uint32_t> args = {};
      for (const auto argument : expr.args) {
        uint32_t id = 0;
        if (!EmitValue(program, argument, &id, error)) {
          return 0;
        }
        args.push_back(id);
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      std::vector<uint32_t> operands = {type, id};
      operands.insert(operands.end(), args.begin(), args.end());
      writer.Inst(spirv::kOpCompositeConstruct, operands);
      return id;
    }
    case IRExprKind::Swizzle: {
      if (expr.args.size() != 1 || expr.swizzle.empty()) {
        Fail(error, "a swizzle needs one source and a swizzle spelling");
        return 0;
      }
      uint32_t source = 0;
      if (!EmitValue(program, expr.args[0], &source, error)) {
        return 0;
      }
      std::vector<uint32_t> indices = {};
      for (const auto component : expr.swizzle) {
        const auto index = SwizzleIndex(component);
        if (index < 0) {
          Fail(error, "the swizzle contains an unsupported component");
          return 0;
        }
        indices.push_back(static_cast<uint32_t>(index));
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      if (expr.type == SLType::Float) {
        if (indices.size() != 1) {
          Fail(error, "a scalar swizzle must select one component");
          return 0;
        }
        writer.Inst(spirv::kOpCompositeExtract, {type, id, source, indices[0]});
      } else {
        std::vector<uint32_t> operands = {type, id, source, source};
        operands.insert(operands.end(), indices.begin(), indices.end());
        writer.Inst(spirv::kOpVectorShuffle, operands);
      }
      return id;
    }
    case IRExprKind::Binary: {
      if (expr.args.size() != 2) {
        Fail(error, "a binary expression needs two operands");
        return 0;
      }
      uint32_t left = 0;
      uint32_t right = 0;
      if (!EmitValue(program, expr.args[0], &left, error) ||
          !EmitValue(program, expr.args[1], &right, error)) {
        return 0;
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      uint16_t opcode = 0;
      if (program.values[expr.args[0]].type == SLType::Float3x3 &&
          program.values[expr.args[1]].type == SLType::Float3 && expr.binaryOp == IRBinaryOp::Mul) {
        opcode = spirv::kOpMatrixTimesVector;
      } else if (IsFloatVector(program.values[expr.args[0]].type) &&
                 IsFloatVector(program.values[expr.args[1]].type)) {
        switch (expr.binaryOp) {
          case IRBinaryOp::Add:
            opcode = spirv::kOpFAdd;
            break;
          case IRBinaryOp::Sub:
            opcode = spirv::kOpFSub;
            break;
          case IRBinaryOp::Mul:
            opcode = spirv::kOpFMul;
            break;
          case IRBinaryOp::Div:
            opcode = spirv::kOpFDiv;
            break;
        }
      }
      if (opcode == 0) {
        Fail(error, "the binary expression is not supported by the SPIR-V builder yet");
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      writer.Inst(opcode, {type, id, left, right});
      return id;
    }
    default:
      Fail(error, "this expression kind is not supported by the phase-2 SPIR-V builder");
      return 0;
  }
}

bool SPIRVShaderBuilder::EmitValue(const ProgramIR& program, ValueID value, uint32_t* resultId,
                                   std::string* error) {
  if (resultId == nullptr) {
    return Fail(error, "the SPIR-V result id is null");
  }
  if (value == INVALID_VALUE_ID || value >= program.values.size()) {
    return Fail(error, "the value has an invalid id");
  }
  const auto found = valueIds.find(value);
  if (found != valueIds.end()) {
    *resultId = found->second;
    return true;
  }
  const auto& irValue = program.values[value];
  if (irValue.definingExpr == INVALID_EXPR_ID || irValue.definingExpr >= program.exprs.size()) {
    return Fail(error, "the value has no defining expression");
  }
  const auto id = EmitExpr(program, program.exprs[irValue.definingExpr], error);
  if (id == 0) {
    return false;
  }
  valueIds.emplace(value, id);
  *resultId = id;
  return true;
}

bool SPIRVShaderBuilder::StoreOutput(const ProgramIR& program, ValueID value, std::string* error) {
  if (!begun) {
    return Fail(error, "the SPIR-V shader builder has not begun");
  }
  uint32_t source = 0;
  if (!EmitValue(program, value, &source, error)) {
    return false;
  }
  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpStore, {outputPointerId, source});
  return true;
}

std::vector<uint32_t> SPIRVShaderBuilder::Finish(std::string* error) {
  if (!begun || finished) {
    Fail(error, "the SPIR-V shader builder is not in a finishable state");
    return {};
  }
  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpReturn, {});
  writer.Inst(spirv::kOpFunctionEnd, {});
  std::vector<uint32_t> out = {};
  writer.Finish(&out);
  finished = true;
  return out;
}

}  // namespace tgfx
