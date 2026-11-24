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
#include <algorithm>

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
    case SLType::Texture2DSampler: {
      if (imageTypeId == 0) {
        const auto floatType = TypeId(SLType::Float, error);
        if (floatType == 0) {
          writer.Sink(saved);
          return 0;
        }
        imageTypeId = writer.NewId();
        writer.Inst(spirv::kOpTypeImage,
                    {imageTypeId, floatType, spirv::kImageDim2D, 0, 0, 0, 1, 0});
        samplerTypeId = writer.NewId();
        writer.Inst(spirv::kOpTypeSampler, {samplerTypeId});
        sampledImageTypeId = writer.NewId();
        writer.Inst(spirv::kOpTypeSampledImage, {sampledImageTypeId, imageTypeId});
      }
      id = sampledImageTypeId;
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

uint32_t SPIRVShaderBuilder::LoadSampler(const ProgramIR& program, ValueID sampler,
                                         std::string* error) {
  const auto found = samplerValues.find(sampler);
  if (found != samplerValues.end()) {
    return found->second;
  }
  if (sampler == INVALID_VALUE_ID || sampler >= program.values.size()) {
    Fail(error, "the sampler has an invalid value");
    return 0;
  }
  const auto& samplerValue = program.values[sampler];
  if (samplerValue.kind != IRValueKind::Sampler || samplerValue.type != SLType::Texture2DSampler) {
    Fail(error, "the SPIR-V builder only supports sampler2D");
    return 0;
  }
  uint32_t binding = 0;
  bool hasBinding = false;
  for (const auto& info : program.interface.samplers) {
    if (info.name == samplerValue.debugName) {
      binding = info.binding;
      hasBinding = true;
      break;
    }
  }
  if (!hasBinding) {
    Fail(error, "the sampler has no interface binding");
    return 0;
  }
  const auto sampledType = TypeId(SLType::Texture2DSampler, error);
  const auto pointer =
      PointerTypeId(SLType::Texture2DSampler, spirv::kStorageUniformConstant, error);
  if (sampledType == 0 || pointer == 0) {
    return 0;
  }

  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto variableId = writer.NewId();
  writer.Inst(spirv::kOpVariable, {pointer, variableId, spirv::kStorageUniformConstant});
  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate, {variableId, spirv::kDecorationDescriptorSet, 0});
  writer.Inst(spirv::kOpDecorate, {variableId, spirv::kDecorationBinding, binding});
  writer.Sink(&writer.func);
  const auto loadId = writer.NewId();
  writer.Inst(spirv::kOpLoad, {sampledType, loadId, variableId});
  writer.Sink(saved);
  samplerValues[sampler] = loadId;
  return loadId;
}

uint32_t SPIRVShaderBuilder::IntConstantId(int32_t value, std::string* error) {
  IRExpr expr = {};
  expr.kind = IRExprKind::ConstantInt;
  expr.type = SLType::Int;
  expr.constantBits = static_cast<uint32_t>(value);
  return ConstantId(expr, error);
}

uint32_t SPIRVShaderBuilder::ArrayTypeId(SLType elementType, uint32_t count, uint32_t stride,
                                         bool decorateStride, std::string* error) {
  const auto elementTypeId = TypeId(elementType, error);
  const auto lengthId = IntConstantId(static_cast<int32_t>(count), error);
  if (elementTypeId == 0 || lengthId == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto id = writer.NewId();
  writer.Inst(spirv::kOpTypeArray, {id, elementTypeId, lengthId});
  if (decorateStride) {
    writer.Sink(&writer.ann);
    writer.Inst(spirv::kOpDecorate, {id, spirv::kDecorationArrayStride, stride});
  }
  writer.Sink(saved);
  return id;
}

uint32_t SPIRVShaderBuilder::FunctionParamTypeId(const OperatorParam& param, std::string* error) {
  if (!param.isArray) {
    return TypeId(param.type, error);
  }
  return ArrayTypeId(param.type, param.arrayCount, 0, false, error);
}

bool SPIRVShaderBuilder::EnsureUniformBlock(const ProgramIR& program, std::string* error) {
  if (uniformBlockDeclared) {
    return true;
  }
  uniformBlockDeclared = true;
  const UniformBlockIR* block = nullptr;
  for (const auto& candidate : program.interface.uniformBlocks) {
    if (candidate.stage == stage) {
      block = &candidate;
      break;
    }
  }
  if (block == nullptr) {
    return true;
  }

  struct BlockMember {
    uint32_t offset = 0;
    const UniformIR* uniform = nullptr;
    const UniformArrayIR* array = nullptr;
  };
  std::vector<BlockMember> members = {};
  for (const auto& uniform : program.interface.uniforms) {
    if (uniform.stage == stage) {
      members.push_back({uniform.offset, &uniform, nullptr});
    }
  }
  for (const auto& array : program.interface.uniformArrays) {
    if (array.stage == stage) {
      members.push_back({array.offset, nullptr, &array});
    }
  }
  if (members.empty()) {
    return true;
  }
  std::sort(members.begin(), members.end(), [](const BlockMember& left, const BlockMember& right) {
    return left.offset < right.offset;
  });

  std::vector<uint32_t> memberTypes = {};
  for (size_t index = 0; index < members.size(); ++index) {
    const auto& member = members[index];
    uint32_t typeId = 0;
    if (member.uniform != nullptr) {
      typeId = TypeId(member.uniform->type, error);
      if (typeId == 0) {
        return false;
      }
      uniformMemberIndices[member.uniform->name] = static_cast<uint32_t>(index);
    } else {
      typeId = ArrayTypeId(member.array->elementType, member.array->count, member.array->stride,
                           true, error);
      if (typeId == 0) {
        return false;
      }
      uniformArrayTypeIds[member.array->name] = typeId;
      uniformMemberIndices[member.array->name] = static_cast<uint32_t>(index);
    }
    memberTypes.push_back(typeId);
  }

  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  uniformBlockTypeId = writer.NewId();
  std::vector<uint32_t> structOperands = {uniformBlockTypeId};
  structOperands.insert(structOperands.end(), memberTypes.begin(), memberTypes.end());
  writer.Inst(spirv::kOpTypeStruct, structOperands);
  uniformBlockPointerId = writer.NewId();
  writer.Inst(spirv::kOpTypePointer,
              {uniformBlockPointerId, spirv::kStorageUniform, uniformBlockTypeId});
  uniformBlockVariableId = writer.NewId();
  writer.Inst(spirv::kOpVariable,
              {uniformBlockPointerId, uniformBlockVariableId, spirv::kStorageUniform});

  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate, {uniformBlockTypeId, spirv::kDecorationBlock});
  writer.Inst(spirv::kOpDecorate, {uniformBlockVariableId, spirv::kDecorationDescriptorSet, 0});
  writer.Inst(spirv::kOpDecorate,
              {uniformBlockVariableId, spirv::kDecorationBinding, block->binding});
  for (size_t index = 0; index < members.size(); ++index) {
    const auto& member = members[index];
    const uint32_t offset =
        member.uniform != nullptr ? member.uniform->offset : member.array->offset;
    writer.Inst(spirv::kOpMemberDecorate, {uniformBlockTypeId, static_cast<uint32_t>(index),
                                           spirv::kDecorationOffset, offset});
    if (member.uniform != nullptr &&
        (member.uniform->type == SLType::Float2x2 || member.uniform->type == SLType::Float3x3 ||
         member.uniform->type == SLType::Float4x4)) {
      writer.Inst(spirv::kOpMemberDecorate, {uniformBlockTypeId, static_cast<uint32_t>(index),
                                             spirv::kDecorationMatrixStride, 16});
      writer.Inst(spirv::kOpMemberDecorate,
                  {uniformBlockTypeId, static_cast<uint32_t>(index), spirv::kDecorationColMajor});
    }
  }
  writer.Sink(saved);
  return true;
}

uint32_t SPIRVShaderBuilder::EmitUniformValue(const ProgramIR& program, const IRValue& value,
                                              std::string* error) {
  if (!EnsureUniformBlock(program, error)) {
    return 0;
  }
  const auto member = uniformMemberIndices.find(value.debugName);
  if (member == uniformMemberIndices.end()) {
    Fail(error, "the uniform has no member in the SPIR-V block");
    return 0;
  }
  const auto typeId = TypeId(value.type, error);
  const auto pointer = PointerTypeId(value.type, spirv::kStorageUniform, error);
  if (typeId == 0 || pointer == 0) {
    return 0;
  }
  const auto memberId = IntConstantId(static_cast<int32_t>(member->second), error);
  if (memberId == 0) {
    return 0;
  }
  writer.Sink(&writer.func);
  const auto accessId = writer.NewId();
  writer.Inst(spirv::kOpAccessChain, {pointer, accessId, uniformBlockVariableId, memberId});
  const auto loadId = writer.NewId();
  writer.Inst(spirv::kOpLoad, {typeId, loadId, accessId});
  return loadId;
}

uint32_t SPIRVShaderBuilder::EmitUniformArrayValue(const ProgramIR& program, const IRValue& value,
                                                   std::string* error) {
  if (!EnsureUniformBlock(program, error)) {
    return 0;
  }
  const auto member = uniformMemberIndices.find(value.debugName);
  if (member == uniformMemberIndices.end()) {
    Fail(error, "the uniform array has no member in the SPIR-V block");
    return 0;
  }
  const UniformArrayIR* array = nullptr;
  for (const auto& candidate : program.interface.uniformArrays) {
    if (candidate.name == value.debugName && candidate.stage == stage) {
      array = &candidate;
      break;
    }
  }
  if (array == nullptr) {
    Fail(error, "the uniform array is missing from the interface");
    return 0;
  }
  const auto type = uniformArrayTypeIds.find(value.debugName);
  if (type == uniformArrayTypeIds.end()) {
    Fail(error, "the uniform array has no SPIR-V type");
    return 0;
  }
  auto pointer = uniformArrayPointerIds.find(value.debugName);
  if (pointer == uniformArrayPointerIds.end()) {
    auto* saved = writer.sink;
    writer.Sink(&writer.decl);
    const auto pointerId = writer.NewId();
    writer.Inst(spirv::kOpTypePointer, {pointerId, spirv::kStorageUniform, type->second});
    writer.Sink(saved);
    pointer = uniformArrayPointerIds.emplace(value.debugName, pointerId).first;
  }
  const auto memberId = IntConstantId(static_cast<int32_t>(member->second), error);
  if (memberId == 0) {
    return 0;
  }
  writer.Sink(&writer.func);
  const auto accessId = writer.NewId();
  writer.Inst(spirv::kOpAccessChain, {pointer->second, accessId, uniformBlockVariableId, memberId});
  const auto loadId = writer.NewId();
  writer.Inst(spirv::kOpLoad, {type->second, loadId, accessId});
  return loadId;
}

bool SPIRVShaderBuilder::Begin(std::string* error) {
  if (begun) {
    return Fail(error, "the SPIR-V shader builder has already begun");
  }
  if (stage != ShaderStage::Fragment && stage != ShaderStage::Vertex) {
    return Fail(error, "the SPIR-V builder supports vertex and fragment shaders only");
  }
  const bool fragment = stage == ShaderStage::Fragment;

  writer.Sink(&writer.pre);
  writer.Inst(spirv::kOpCapability, {spirv::kCapabilityShader});
  writer.Inst(spirv::kOpCapability, {spirv::kCapabilityLinkage});
  writer.Inst(spirv::kOpMemoryModel, {0, 1});

  mainId = writer.NewId();
  outputId = writer.NewId();
  if (fragment) {
    fragCoordId = writer.NewId();
  }
  writer.Sink(&writer.mode);
  {
    auto operands = std::vector<uint32_t>{
        fragment ? spirv::kExecutionModelFragment : spirv::kExecutionModelVertex, mainId};
    const auto name = spirv::NameWords("main");
    operands.insert(operands.end(), name.begin(), name.end());
    operands.push_back(outputId);
    if (fragment) {
      operands.push_back(fragCoordId);
    }
    writer.Inst(spirv::kOpEntryPoint, operands);
  }
  if (fragment) {
    writer.Inst(spirv::kOpExecutionMode, {mainId, spirv::kExecutionModeOriginUpperLeft});
  }

  writer.Sink(&writer.debug);
  {
    auto operands = std::vector<uint32_t>{mainId};
    const auto name = spirv::NameWords("main");
    operands.insert(operands.end(), name.begin(), name.end());
    writer.Inst(spirv::kOpName, operands);
  }
  {
    auto operands = std::vector<uint32_t>{outputId};
    const auto name = spirv::NameWords(fragment ? "outputColor" : "gl_Position");
    operands.insert(operands.end(), name.begin(), name.end());
    writer.Inst(spirv::kOpName, operands);
  }

  writer.Sink(&writer.ann);
  if (fragment) {
    writer.Inst(spirv::kOpDecorate, {outputId, spirv::kDecorationLocation, 0});
    writer.Inst(spirv::kOpDecorate,
                {fragCoordId, spirv::kDecorationBuiltIn, spirv::kBuiltInFragCoord});
  } else {
    writer.Inst(spirv::kOpDecorate, {outputId, spirv::kDecorationBuiltIn, spirv::kBuiltInPosition});
  }

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
  if (fragment) {
    const auto inputPointer = PointerTypeId(SLType::Float4, spirv::kStorageInput, error);
    if (inputPointer == 0) {
      return false;
    }
    writer.Inst(spirv::kOpVariable, {inputPointer, fragCoordId, spirv::kStorageInput});
  }
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

bool SPIRVShaderBuilder::DeclareLocals(const ProgramIR& program, std::string* error) {
  if (!begun) {
    return Fail(error, "the SPIR-V shader builder has not begun");
  }
  for (const auto& value : program.values) {
    if (value.kind != IRValueKind::Local || value.definingExpr != INVALID_EXPR_ID ||
        valueIds.find(value.id) != valueIds.end()) {
      continue;
    }
    const auto pointer = PointerTypeId(value.type, spirv::kStorageFunction, error);
    if (pointer == 0) {
      return false;
    }
    writer.Sink(&writer.func);
    const auto variableId = writer.NewId();
    writer.Inst(spirv::kOpVariable, {pointer, variableId, spirv::kStorageFunction});
    valueIds[value.id] = variableId;
  }
  return true;
}

bool SPIRVShaderBuilder::Assign(const ProgramIR& program, const LValue& dst, ValueID value,
                                std::string* error) {
  if (!begun) {
    return Fail(error, "the SPIR-V shader builder has not begun");
  }
  if (!dst.swizzle.empty()) {
    return Fail(error, "the SPIR-V builder does not support swizzled assignment yet");
  }
  uint32_t target = 0;
  uint32_t source = 0;
  if (!EmitValue(program, dst.base, &target, error) || !EmitValue(program, value, &source, error)) {
    return false;
  }
  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpStore, {target, source});
  return true;
}

uint32_t SPIRVShaderBuilder::EmitExpr(const ProgramIR& program, const IRExpr& expr,
                                      std::string* error) {
  switch (expr.kind) {
    case IRExprKind::ConstantFloat:
    case IRExprKind::ConstantInt:
      return ConstantId(expr, error);
    case IRExprKind::Load: {
      if (expr.args.size() != 1) {
        Fail(error, "a load needs one source");
        return 0;
      }
      if (program.values[expr.args[0]].kind == IRValueKind::Uniform) {
        uint32_t loaded = 0;
        return EmitValue(program, expr.args[0], &loaded, error) ? loaded : 0;
      }
      uint32_t source = 0;
      if (!EmitValue(program, expr.args[0], &source, error)) {
        return 0;
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      writer.Inst(spirv::kOpLoad, {type, id, source});
      return id;
    }
    case IRExprKind::Builtin: {
      if (stage != ShaderStage::Fragment || expr.builtin != "gl_FragCoord") {
        Fail(error, "the builtin read is not supported by the SPIR-V builder yet");
        return 0;
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      writer.Inst(spirv::kOpLoad, {type, id, fragCoordId});
      return id;
    }
    case IRExprKind::TextureSample: {
      if (stage != ShaderStage::Fragment) {
        Fail(error, "implicit-lod texture sampling is only available in fragment shaders");
        return 0;
      }
      if (expr.args.size() != 2) {
        Fail(error, "a texture sample needs a sampler and a coordinate");
        return 0;
      }
      const auto sampledImage = LoadSampler(program, expr.args[0], error);
      uint32_t coordinate = 0;
      if (sampledImage == 0 || !EmitValue(program, expr.args[1], &coordinate, error)) {
        return 0;
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      writer.Inst(spirv::kOpImageSampleImplicitLod, {type, id, sampledImage, coordinate});
      return id;
    }
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
  if (irValue.kind == IRValueKind::Uniform) {
    const auto id = EmitUniformValue(program, irValue, error);
    if (id == 0) {
      return false;
    }
    valueIds.emplace(value, id);
    *resultId = id;
    return true;
  }
  if (irValue.kind == IRValueKind::UniformArray) {
    const auto id = EmitUniformArrayValue(program, irValue, error);
    if (id == 0) {
      return false;
    }
    valueIds.emplace(value, id);
    *resultId = id;
    return true;
  }
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

uint32_t SPIRVShaderBuilder::ImportFunction(const OperatorDesc& desc,
                                            const OperatorArtifact& artifact, std::string* error) {
  if (!begun) {
    Fail(error, "the SPIR-V shader builder has not begun");
    return 0;
  }
  if (artifact.backend != OperatorBackend::SPIRV || artifact.symbol.empty()) {
    Fail(error, "the operator bundle has no SPIR-V function for this operator");
    return 0;
  }
  for (const auto& existing : importedFunctions) {
    if (existing.symbol == artifact.symbol) {
      return existing.id;
    }
  }

  ImportedFunction imported = {};
  imported.symbol = artifact.symbol;
  imported.returnTypeId = TypeId(desc.returnType, error);
  if (imported.returnTypeId == 0) {
    return 0;
  }
  for (const auto& param : desc.params) {
    const auto typeId = FunctionParamTypeId(param, error);
    if (typeId == 0) {
      return 0;
    }
    imported.paramTypeIds.push_back(typeId);
  }

  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  imported.functionType = writer.NewId();
  std::vector<uint32_t> functionTypeOperands = {imported.functionType, imported.returnTypeId};
  functionTypeOperands.insert(functionTypeOperands.end(), imported.paramTypeIds.begin(),
                              imported.paramTypeIds.end());
  writer.Inst(spirv::kOpTypeFunction, functionTypeOperands);
  imported.id = writer.NewId();
  writer.Sink(saved);
  importedFunctions.push_back(std::move(imported));
  return importedFunctions.back().id;
}

uint32_t SPIRVShaderBuilder::EmitFunctionCall(ValueID result, SLType returnType,
                                              const std::vector<uint32_t>& args,
                                              uint32_t functionId, std::string* error) {
  if (!begun) {
    Fail(error, "the SPIR-V shader builder has not begun");
    return 0;
  }
  if (functionId == 0) {
    Fail(error, "the SPIR-V operator function has no id");
    return 0;
  }
  const auto typeId = TypeId(returnType, error);
  if (typeId == 0) {
    return 0;
  }
  writer.Sink(&writer.func);
  const auto resultId = writer.NewId();
  std::vector<uint32_t> operands = {typeId, resultId, functionId};
  operands.insert(operands.end(), args.begin(), args.end());
  writer.Inst(spirv::kOpFunctionCall, operands);
  valueIds[result] = resultId;
  return resultId;
}

std::vector<uint32_t> SPIRVShaderBuilder::Finish(std::string* error) {
  if (!begun || finished) {
    Fail(error, "the SPIR-V shader builder is not in a finishable state");
    return {};
  }
  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpReturn, {});
  writer.Inst(spirv::kOpFunctionEnd, {});
  auto mainFunction = std::move(writer.func);
  writer.func.clear();

  for (const auto& imported : importedFunctions) {
    writer.Sink(&writer.ann);
    std::vector<uint32_t> decoration = {imported.id, spirv::kDecorationLinkageAttributes};
    const auto name = spirv::NameWords(imported.symbol.c_str());
    decoration.insert(decoration.end(), name.begin(), name.end());
    decoration.push_back(spirv::kLinkageTypeImport);
    writer.Inst(spirv::kOpDecorate, decoration);

    writer.Sink(&writer.func);
    writer.Inst(spirv::kOpFunction, {imported.returnTypeId, imported.id, 0, imported.functionType});
    for (const auto paramType : imported.paramTypeIds) {
      const auto parameterId = writer.NewId();
      writer.Inst(spirv::kOpFunctionParameter, {paramType, parameterId});
    }
    writer.Inst(spirv::kOpFunctionEnd, {});
  }
  writer.func.insert(writer.func.end(), mainFunction.begin(), mainFunction.end());

  std::vector<uint32_t> out = {};
  writer.Finish(&out);
  finished = true;
  return out;
}

SPIRVFragmentShaderBuilder::SPIRVFragmentShaderBuilder()
    : SPIRVShaderBuilder(ShaderStage::Fragment) {
}

SPIRVVertexShaderBuilder::SPIRVVertexShaderBuilder() : SPIRVShaderBuilder(ShaderStage::Vertex) {
}

}  // namespace tgfx
