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
#include "SPIRVInterfaceLayout.h"
#if defined(TGFX_USE_VULKAN)
#include "gpu/spirv/SPIRVLinker.h"
#endif

namespace tgfx {
namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) {
    *error = message;
  }
  return false;
}

bool Fail(std::string* error, const std::string& message) {
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

uint32_t SPIRVShaderBuilder::BoolTypeId(std::string* error) {
  if (boolTypeId != 0) {
    return boolTypeId;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  boolTypeId = writer.NewId();
  writer.Inst(spirv::kOpTypeBool, {boolTypeId});
  writer.Sink(saved);
  if (boolTypeId == 0 && error != nullptr) {
    *error = "the SPIR-V bool type could not be declared";
  }
  return boolTypeId;
}

uint32_t SPIRVShaderBuilder::BoolVectorTypeId(uint32_t components, std::string* error) {
  const auto found = boolVectorTypeIds.find(components);
  if (found != boolVectorTypeIds.end()) {
    return found->second;
  }
  const auto scalar = BoolTypeId(error);
  if (scalar == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto id = writer.NewId();
  writer.Inst(spirv::kOpTypeVector, {id, scalar, components});
  writer.Sink(saved);
  boolVectorTypeIds.emplace(components, id);
  return id;
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

uint32_t SPIRVShaderBuilder::PointerTypeIdFor(uint32_t pointeeType, uint32_t storageClass,
                                              std::string* error) {
  if (pointeeType == 0) {
    Fail(error, "the pointer's pointee type is invalid");
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto id = writer.NewId();
  writer.Inst(spirv::kOpTypePointer, {id, storageClass, pointeeType});
  writer.Sink(saved);
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

void SPIRVShaderBuilder::AddEntryPointInterface(uint32_t id) {
  for (size_t index = 0; index < writer.mode.size();) {
    const auto instruction = writer.mode[index];
    const auto wordCount = instruction >> 16;
    const auto opcode = instruction & 0xFFFFu;
    if (wordCount == 0) {
      return;
    }
    if (opcode == spirv::kOpEntryPoint) {
      writer.mode.insert(writer.mode.begin() + static_cast<ptrdiff_t>(index + wordCount), id);
      writer.mode[index] = ((wordCount + 1) << 16) | opcode;
      return;
    }
    index += wordCount;
  }
}

uint32_t SPIRVShaderBuilder::EnsureAttribute(const ProgramIR& program, const IRValue& value,
                                             std::string* error) {
  const AttributeIR* attribute = nullptr;
  for (const auto& candidate : program.interface.attributes) {
    if (candidate.name == value.debugName) {
      attribute = &candidate;
      break;
    }
  }
  if (attribute == nullptr) {
    Fail(error, "the vertex attribute is missing from the interface");
    return 0;
  }
  const auto typeId = TypeId(value.type, error);
  const auto pointer = PointerTypeId(value.type, spirv::kStorageInput, error);
  if (typeId == 0 || pointer == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto variableId = writer.NewId();
  writer.Inst(spirv::kOpVariable, {pointer, variableId, spirv::kStorageInput});
  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate, {variableId, spirv::kDecorationLocation, attribute->location});
  writer.Sink(&writer.func);
  const auto loadId = writer.NewId();
  writer.Inst(spirv::kOpLoad, {typeId, loadId, variableId});
  writer.Sink(saved);
  AddEntryPointInterface(variableId);
  return loadId;
}

uint32_t SPIRVShaderBuilder::EnsureVarying(const ProgramIR& program, const IRValue& value,
                                           std::string* error) {
  const auto found = varyingVariables.find(value.id);
  if (found != varyingVariables.end()) {
    return found->second;
  }
  const VaryingIR* varying = nullptr;
  for (const auto& candidate : program.interface.varyings) {
    if (candidate.name == value.debugName) {
      varying = &candidate;
      break;
    }
  }
  if (varying == nullptr) {
    Fail(error, "the varying is missing from the interface");
    return 0;
  }
  const uint32_t storage =
      stage == ShaderStage::Vertex ? spirv::kStorageOutput : spirv::kStorageInput;
  const auto typeId = TypeId(value.type, error);
  const auto pointer = PointerTypeId(value.type, storage, error);
  if (typeId == 0 || pointer == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto variableId = writer.NewId();
  writer.Inst(spirv::kOpVariable, {pointer, variableId, storage});
  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate, {variableId, spirv::kDecorationLocation, varying->location});
  writer.Sink(saved);
  AddEntryPointInterface(variableId);
  varyingVariables[value.id] = variableId;
  return variableId;
}

bool SPIRVShaderBuilder::StoreVarying(const ProgramIR& program, const LValue& dst, ValueID value,
                                      std::string* error) {
  if (stage != ShaderStage::Vertex) {
    return Fail(error, "varying stores are only valid in vertex shaders");
  }
  if (!dst.swizzle.empty()) {
    return Fail(error, "the SPIR-V builder does not support swizzled varying stores yet");
  }
  if (dst.base == INVALID_VALUE_ID || dst.base >= program.values.size() ||
      program.values[dst.base].kind != IRValueKind::Varying) {
    return Fail(error, "the varying store target is not a varying");
  }
  const auto variable = EnsureVarying(program, program.values[dst.base], error);
  if (variable == 0) {
    return false;
  }
  const auto source = EmitValueAsValue(program, value, error);
  if (source == 0) {
    return false;
  }
  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpStore, {variable, source});
  return true;
}

uint32_t SPIRVShaderBuilder::EmitValueAsValue(const ProgramIR& program, ValueID value,
                                              std::string* error) {
  uint32_t id = 0;
  if (!EmitValue(program, value, &id, error)) {
    return 0;
  }
  if (value == INVALID_VALUE_ID || value >= program.values.size()) {
    return id;
  }
  const auto& irValue = program.values[value];
  if (irValue.kind == IRValueKind::Local && irValue.definingExpr == INVALID_EXPR_ID) {
    const auto type = TypeId(irValue.type, error);
    if (type == 0) {
      return 0;
    }
    writer.Sink(&writer.func);
    const auto loaded = writer.NewId();
    writer.Inst(spirv::kOpLoad, {type, loaded, id});
    return loaded;
  }
  return id;
}

uint32_t SPIRVShaderBuilder::EnsureSamplerVariable(const ProgramIR& program, const IRValue& value,
                                                   std::string* error) {
  const auto found = samplerVariables.find(value.debugName);
  if (found != samplerVariables.end()) {
    return found->second;
  }
  uint32_t binding = 0;
  bool hasBinding = false;
  for (const auto& info : program.interface.samplers) {
    if (info.name == value.debugName) {
      binding = info.binding;
      hasBinding = true;
      break;
    }
  }
  if (!hasBinding) {
    Fail(error, "the sampler has no interface binding");
    return 0;
  }
  const auto pointer =
      PointerTypeId(SLType::Texture2DSampler, spirv::kStorageUniformConstant, error);
  if (pointer == 0) {
    return 0;
  }
  auto* saved = writer.sink;
  writer.Sink(&writer.decl);
  const auto variableId = writer.NewId();
  writer.Inst(spirv::kOpVariable, {pointer, variableId, spirv::kStorageUniformConstant});
  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate,
              {variableId, spirv::kDecorationDescriptorSet, SPIRVInterfaceLayout::SamplerSet()});
  writer.Inst(spirv::kOpDecorate, {variableId, spirv::kDecorationBinding, binding});
  writer.Sink(saved);
  samplerVariables.emplace(value.debugName, variableId);
  return variableId;
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
  const auto variableId = EnsureSamplerVariable(program, samplerValue, error);
  if (variableId == 0) {
    return 0;
  }
  const auto sampledType = TypeId(SLType::Texture2DSampler, error);
  if (sampledType == 0) {
    return 0;
  }
  writer.Sink(&writer.func);
  const auto loadId = writer.NewId();
  writer.Inst(spirv::kOpLoad, {sampledType, loadId, variableId});
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
  // The imported module is the tool's compilation of the GLSL function, whose parameters are
  // storage pointers: a UniformConstant pointer for a sampler (the descriptor slot), a Function
  // pointer for every plain value. The declared types must match the module's exports word for
  // word or the linker reports a type mismatch on the symbol.
  if (param.type == SLType::Texture2DSampler || param.type == SLType::Texture2DRectSampler) {
    return PointerTypeId(param.type, spirv::kStorageUniformConstant, error);
  }
  const auto valueType = param.isArray ? ArrayTypeId(param.type, param.arrayCount, 0, false, error)
                                       : TypeId(param.type, error);
  if (valueType == 0) {
    return 0;
  }
  return PointerTypeIdFor(valueType, spirv::kStorageFunction, error);
}

void SPIRVShaderBuilder::EnsureLinkageCapability() {
  if (linkageCapabilityAdded) {
    return;
  }
  linkageCapabilityAdded = true;
  // OpCapability Shader occupies the first two words; keep the capability before the memory model.
  const uint32_t instruction = (2u << 16) | spirv::kOpCapability;
  writer.pre.insert(writer.pre.begin() + 2, {instruction, spirv::kCapabilityLinkage});
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

  const uint32_t descriptorSet = SPIRVInterfaceLayout::UniformSet(stage);
  writer.Sink(&writer.ann);
  writer.Inst(spirv::kOpDecorate, {uniformBlockTypeId, spirv::kDecorationBlock});
  writer.Inst(spirv::kOpDecorate,
              {uniformBlockVariableId, spirv::kDecorationDescriptorSet, descriptorSet});
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
  if (!EmitValue(program, dst.base, &target, error)) {
    return false;
  }
  const auto source = EmitValueAsValue(program, value, error);
  if (source == 0) {
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
      const auto sourceKind = program.values[expr.args[0]].kind;
      if (sourceKind == IRValueKind::Uniform || sourceKind == IRValueKind::Attribute) {
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
      const auto& coordinateValue = program.values[expr.args[1]];
      if (coordinateValue.kind == IRValueKind::Local &&
          coordinateValue.definingExpr == INVALID_EXPR_ID) {
        const auto coordinateType = TypeId(coordinateValue.type, error);
        if (coordinateType == 0) {
          return 0;
        }
        writer.Sink(&writer.func);
        const auto loaded = writer.NewId();
        writer.Inst(spirv::kOpLoad, {coordinateType, loaded, coordinate});
        coordinate = loaded;
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      writer.Sink(&writer.func);
      const auto id = writer.NewId();
      writer.Inst(spirv::kOpImageSampleImplicitLod, {type, id, sampledImage, coordinate});
      if (expr.samplerSwizzleCode <= 0 || expr.samplerSwizzleCode > 5 ||
          type != TypeId(SLType::Float4, error)) {
        return id;
      }
      static constexpr uint32_t swizzleIndices[6][4] = {
          {0, 1, 2, 3}, {0, 0, 0, 0}, {0, 0, 0, 3}, {0, 1, 0, 1}, {3, 3, 3, 3}, {2, 1, 0, 3},
      };
      const auto swizzled = writer.NewId();
      const auto& indices = swizzleIndices[expr.samplerSwizzleCode];
      writer.Inst(spirv::kOpVectorShuffle,
                  {type, swizzled, id, id, indices[0], indices[1], indices[2], indices[3]});
      return swizzled;
    }
    case IRExprKind::Call: {
      const auto* desc = FindOperator(expr.op);
      if (desc == nullptr) {
        Fail(error, "the operator is missing from the manifest");
        return 0;
      }
      auto loadArgument = [&](ValueID value, uint32_t* id) {
        if (!EmitValue(program, value, id, error)) {
          return false;
        }
        const auto& irValue = program.values[value];
        if (irValue.kind == IRValueKind::Local && irValue.definingExpr == INVALID_EXPR_ID) {
          const auto type = TypeId(irValue.type, error);
          if (type == 0) {
            return false;
          }
          writer.Sink(&writer.func);
          const auto loaded = writer.NewId();
          writer.Inst(spirv::kOpLoad, {type, loaded, *id});
          *id = loaded;
        }
        return true;
      };
      if (operatorBundle != nullptr) {
        const auto* artifact = operatorBundle->Find(expr.op, OperatorBackend::SPIRV);
        if (artifact != nullptr && !artifact->spirv.empty()) {
          const auto functionId = ImportFunction(*desc, *artifact, error);
          if (functionId == 0) {
            return 0;
          }
          std::vector<uint32_t> arguments = {};
          for (const auto argument : expr.args) {
            uint32_t id = 0;
            if (!loadArgument(argument, &id)) {
              return 0;
            }
            arguments.push_back(id);
          }
          return EmitFunctionCall(expr.result, expr.type, arguments, functionId, error);
        }
      }
      auto arg = [&](size_t index, uint32_t* id) {
        return index < expr.args.size() && loadArgument(expr.args[index], id);
      };
      auto splat = [&](uint32_t scalar, SLType vectorType, uint32_t* id) {
        const auto type = TypeId(vectorType, error);
        if (type == 0) {
          return false;
        }
        writer.Sink(&writer.func);
        *id = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {type, *id, scalar, scalar, scalar, scalar});
        return true;
      };
      auto extractScalar = [&](uint32_t source, uint32_t index, uint32_t* id) {
        const auto type = TypeId(SLType::Float, error);
        if (type == 0) {
          return false;
        }
        writer.Sink(&writer.func);
        *id = writer.NewId();
        writer.Inst(spirv::kOpCompositeExtract, {type, *id, source, index});
        return true;
      };
      auto extractVector = [&](uint32_t source, SLType type, const std::vector<uint32_t>& indices,
                               uint32_t* id) {
        const auto typeId = TypeId(type, error);
        if (typeId == 0) {
          return false;
        }
        writer.Sink(&writer.func);
        *id = writer.NewId();
        std::vector<uint32_t> operands = {typeId, *id, source, source};
        operands.insert(operands.end(), indices.begin(), indices.end());
        writer.Inst(spirv::kOpVectorShuffle, operands);
        return true;
      };
      auto floatConstant = [&](float value) {
        IRExpr constant = {};
        constant.kind = IRExprKind::ConstantFloat;
        constant.type = SLType::Float;
        constant.constantBits = FloatBits(value);
        return ConstantId(constant, error);
      };
      auto conditionType = [&](uint32_t type) {
        if (type == TypeId(SLType::Float2, error)) {
          return BoolVectorTypeId(2, error);
        }
        if (type == TypeId(SLType::Float3, error)) {
          return BoolVectorTypeId(3, error);
        }
        if (type == TypeId(SLType::Float4, error)) {
          return BoolVectorTypeId(4, error);
        }
        return BoolTypeId(error);
      };
      auto floatMin = [&](uint32_t type, uint32_t left, uint32_t right, uint32_t* result) {
        const auto boolType = conditionType(type);
        if (boolType == 0) {
          return false;
        }
        writer.Sink(&writer.func);
        const auto less = writer.NewId();
        writer.Inst(spirv::kOpFOrdLessThan, {boolType, less, left, right});
        *result = writer.NewId();
        writer.Inst(spirv::kOpSelect, {type, *result, less, left, right});
        return true;
      };
      auto floatMax = [&](uint32_t type, uint32_t left, uint32_t right, uint32_t* result) {
        const auto boolType = conditionType(type);
        if (boolType == 0) {
          return false;
        }
        writer.Sink(&writer.func);
        const auto greater = writer.NewId();
        writer.Inst(spirv::kOpFOrdGreaterThan, {boolType, greater, left, right});
        *result = writer.NewId();
        writer.Inst(spirv::kOpSelect, {type, *result, greater, left, right});
        return true;
      };
      if (expr.op == OperatorID::ColorMatrix) {
        uint32_t color = 0;
        uint32_t matrix = 0;
        uint32_t translate = 0;
        if (!arg(0, &color) || !arg(1, &matrix) || !arg(2, &translate)) {
          return 0;
        }
        const auto floatType = TypeId(SLType::Float, error);
        const auto float3 = TypeId(SLType::Float3, error);
        const auto float4 = TypeId(SLType::Float4, error);
        if (floatType == 0 || float3 == 0 || float4 == 0) {
          return 0;
        }
        uint32_t alpha = 0;
        uint32_t rgb = 0;
        if (!extractScalar(color, 3, &alpha) ||
            !extractVector(color, SLType::Float3, {0, 1, 2}, &rgb)) {
          return 0;
        }
        const auto epsilon = floatConstant(1.0e-4f);
        if (epsilon == 0) {
          return 0;
        }
        writer.Sink(&writer.func);
        uint32_t maxAlpha = 0;
        if (!floatMax(floatType, alpha, epsilon, &maxAlpha)) {
          return 0;
        }
        uint32_t alpha4 = 0;
        uint32_t alpha3 = 0;
        if (!splat(maxAlpha, SLType::Float4, &alpha4) ||
            !extractVector(alpha4, SLType::Float3, {0, 1, 2}, &alpha3)) {
          return 0;
        }
        const auto normalizedRgb = writer.NewId();
        writer.Inst(spirv::kOpFDiv, {float3, normalizedRgb, rgb, alpha3});
        const auto normalized = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {float4, normalized, normalizedRgb, alpha});
        const auto transformed = writer.NewId();
        writer.Inst(spirv::kOpMatrixTimesVector, {float4, transformed, matrix, normalized});
        const auto shifted = writer.NewId();
        writer.Inst(spirv::kOpFAdd, {float4, shifted, transformed, translate});
        const auto zero = floatConstant(0.0f);
        const auto one = floatConstant(1.0f);
        if (zero == 0 || one == 0) {
          return 0;
        }
        uint32_t zero4 = 0;
        uint32_t one4 = 0;
        if (!splat(zero, SLType::Float4, &zero4) || !splat(one, SLType::Float4, &one4)) {
          return 0;
        }
        writer.Sink(&writer.func);
        uint32_t clampedLow = 0;
        uint32_t clamped = 0;
        if (!floatMax(float4, shifted, zero4, &clampedLow) ||
            !floatMin(float4, clampedLow, one4, &clamped)) {
          return 0;
        }
        const auto clampedRgb = writer.NewId();
        writer.Inst(spirv::kOpVectorShuffle, {float3, clampedRgb, clamped, clamped, 0, 1, 2});
        const auto clampedAlpha = writer.NewId();
        writer.Inst(spirv::kOpCompositeExtract, {floatType, clampedAlpha, clamped, 3});
        const auto premultiplied = writer.NewId();
        writer.Inst(spirv::kOpVectorTimesScalar, {float3, premultiplied, clampedRgb, clampedAlpha});
        const auto result = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {float4, result, premultiplied, clampedAlpha});
        return result;
      }
      if (expr.op == OperatorID::AlphaThreshold) {
        uint32_t color = 0;
        uint32_t threshold = 0;
        if (!arg(0, &color) || !arg(1, &threshold)) {
          return 0;
        }
        const auto floatType = TypeId(SLType::Float, error);
        const auto float3 = TypeId(SLType::Float3, error);
        const auto float4 = TypeId(SLType::Float4, error);
        const auto boolType = BoolTypeId(error);
        if (floatType == 0 || float3 == 0 || float4 == 0 || boolType == 0) {
          return 0;
        }
        uint32_t alpha = 0;
        uint32_t rgb = 0;
        if (!extractScalar(color, 3, &alpha) ||
            !extractVector(color, SLType::Float3, {0, 1, 2}, &rgb)) {
          return 0;
        }
        uint32_t alpha4 = 0;
        uint32_t alpha3 = 0;
        if (!splat(alpha, SLType::Float4, &alpha4) ||
            !extractVector(alpha4, SLType::Float3, {0, 1, 2}, &alpha3)) {
          return 0;
        }
        const auto zero = floatConstant(0.0f);
        const auto one = floatConstant(1.0f);
        if (zero == 0 || one == 0) {
          return 0;
        }
        writer.Sink(&writer.func);
        const auto normalizedRgb = writer.NewId();
        writer.Inst(spirv::kOpFDiv, {float3, normalizedRgb, rgb, alpha3});
        const auto thresholdGreater = writer.NewId();
        writer.Inst(spirv::kOpFOrdGreaterThan, {boolType, thresholdGreater, threshold, alpha});
        const auto step = writer.NewId();
        writer.Inst(spirv::kOpSelect, {floatType, step, thresholdGreater, zero, one});
        const auto result = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {float4, result, normalizedRgb, step});
        uint32_t zero4 = 0;
        uint32_t one4 = 0;
        if (!splat(zero, SLType::Float4, &zero4) || !splat(one, SLType::Float4, &one4)) {
          return 0;
        }
        uint32_t clampedLow = 0;
        uint32_t clamped = 0;
        if (!floatMax(float4, result, zero4, &clampedLow) ||
            !floatMin(float4, clampedLow, one4, &clamped)) {
          return 0;
        }
        const auto alphaPositive = writer.NewId();
        writer.Inst(spirv::kOpFOrdGreaterThan, {boolType, alphaPositive, alpha, zero});
        const auto finalResult = writer.NewId();
        writer.Inst(spirv::kOpSelect, {float4, finalResult, alphaPositive, clamped, zero4});
        return finalResult;
      }
      if (expr.op == OperatorID::ConstColor) {
        uint32_t constColor = 0;
        uint32_t inputColor = 0;
        uint32_t mode = 0;
        if (!arg(0, &constColor) || !arg(1, &inputColor) || !arg(2, &mode)) {
          return 0;
        }
        const auto float4 = TypeId(SLType::Float4, error);
        const auto boolType = BoolTypeId(error);
        if (float4 == 0 || boolType == 0) {
          return 0;
        }
        uint32_t alpha = 0;
        uint32_t alphaSplat = 0;
        if (!extractScalar(inputColor, 3, &alpha) || !splat(alpha, SLType::Float4, &alphaSplat)) {
          return 0;
        }
        writer.Sink(&writer.func);
        const auto multiplied = writer.NewId();
        writer.Inst(spirv::kOpFMul, {float4, multiplied, constColor, inputColor});
        const auto alphaMultiplied = writer.NewId();
        writer.Inst(spirv::kOpFMul, {float4, alphaMultiplied, constColor, alphaSplat});
        const auto modeOne = IntConstantId(1, error);
        const auto modeTwo = IntConstantId(2, error);
        if (modeOne == 0 || modeTwo == 0) {
          return 0;
        }
        const auto isModeOne = writer.NewId();
        writer.Inst(spirv::kOpIEqual, {boolType, isModeOne, mode, modeOne});
        const auto isModeTwo = writer.NewId();
        writer.Inst(spirv::kOpIEqual, {boolType, isModeTwo, mode, modeTwo});
        const auto withAlpha = writer.NewId();
        writer.Inst(spirv::kOpSelect, {float4, withAlpha, isModeTwo, alphaMultiplied, constColor});
        const auto result = writer.NewId();
        writer.Inst(spirv::kOpSelect, {float4, result, isModeOne, multiplied, withAlpha});
        return result;
      }
      if (expr.op == OperatorID::Luma) {
        uint32_t color = 0;
        uint32_t kr = 0;
        uint32_t kg = 0;
        uint32_t kb = 0;
        if (!arg(0, &color) || !arg(1, &kr) || !arg(2, &kg) || !arg(3, &kb)) {
          return 0;
        }
        const auto float3 = TypeId(SLType::Float3, error);
        const auto float4 = TypeId(SLType::Float4, error);
        const auto floatType = TypeId(SLType::Float, error);
        if (float3 == 0 || float4 == 0 || floatType == 0) {
          return 0;
        }
        writer.Sink(&writer.func);
        const auto rgb = writer.NewId();
        writer.Inst(spirv::kOpVectorShuffle, {float3, rgb, color, color, 0, 1, 2});
        const auto weights = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {float3, weights, kr, kg, kb});
        const auto luma = writer.NewId();
        writer.Inst(spirv::kOpDot, {floatType, luma, rgb, weights});
        const auto result = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {float4, result, luma, luma, luma, luma});
        return result;
      }
      if (expr.op == OperatorID::DeviceCoord) {
        uint32_t matrix = 0;
        uint32_t coord = 0;
        if (!arg(0, &matrix) || !arg(1, &coord)) {
          return 0;
        }
        const auto float3 = TypeId(SLType::Float3, error);
        const auto float2 = TypeId(SLType::Float2, error);
        if (float3 == 0 || float2 == 0) {
          return 0;
        }
        IRExpr oneExpr = {};
        oneExpr.kind = IRExprKind::ConstantFloat;
        oneExpr.type = SLType::Float;
        oneExpr.constantBits = FloatBits(1.0f);
        const auto one = ConstantId(oneExpr, error);
        if (one == 0) {
          return 0;
        }
        writer.Sink(&writer.func);
        const auto position = writer.NewId();
        writer.Inst(spirv::kOpCompositeConstruct, {float3, position, coord, one});
        const auto transformed = writer.NewId();
        writer.Inst(spirv::kOpMatrixTimesVector, {float3, transformed, matrix, position});
        const auto result = writer.NewId();
        writer.Inst(spirv::kOpVectorShuffle, {float2, result, transformed, transformed, 0, 1});
        return result;
      }
      if (expr.op == OperatorID::MulAlpha || expr.op == OperatorID::AlphaModulate) {
        uint32_t first = 0;
        uint32_t second = 0;
        if (!arg(0, &first) || !arg(1, &second)) {
          return 0;
        }
        const auto alphaSource = expr.op == OperatorID::MulAlpha ? second : first;
        const auto colorSource = expr.op == OperatorID::MulAlpha ? first : second;
        uint32_t alpha = 0;
        uint32_t splatted = 0;
        if (!extractScalar(alphaSource, 3, &alpha) || !splat(alpha, SLType::Float4, &splatted)) {
          return 0;
        }
        const auto float4 = TypeId(SLType::Float4, error);
        if (float4 == 0) {
          return 0;
        }
        writer.Sink(&writer.func);
        const auto result = writer.NewId();
        writer.Inst(spirv::kOpFMul, {float4, result, colorSource, splatted});
        return result;
      }
      if (expr.op == OperatorID::Clamp) {
        uint32_t value = 0;
        uint32_t low = 0;
        uint32_t high = 0;
        if (!arg(0, &value) || !arg(1, &low) || !arg(2, &high)) {
          return 0;
        }
        const auto type = TypeId(expr.type, error);
        if (type == 0) {
          return 0;
        }
        uint32_t low4 = 0;
        uint32_t high4 = 0;
        if (!splat(low, SLType::Float4, &low4) || !splat(high, SLType::Float4, &high4)) {
          return 0;
        }
        writer.Sink(&writer.func);
        uint32_t upper = 0;
        uint32_t result = 0;
        if (!floatMax(type, value, low4, &upper) || !floatMin(type, upper, high4, &result)) {
          return 0;
        }
        return result;
      }
      Fail(error, std::string("the operator '") + desc->debugName + "' has no SPIR-V lowering yet");
      return 0;
    }
    case IRExprKind::Construct: {
      std::vector<uint32_t> args = {};
      for (const auto argument : expr.args) {
        const auto id = EmitValueAsValue(program, argument, error);
        if (id == 0) {
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
      const auto source = EmitValueAsValue(program, expr.args[0], error);
      if (source == 0) {
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
      const auto left = EmitValueAsValue(program, expr.args[0], error);
      const auto right = EmitValueAsValue(program, expr.args[1], error);
      if (left == 0 || right == 0) {
        return 0;
      }
      const auto type = TypeId(expr.type, error);
      if (type == 0) {
        return 0;
      }
      uint16_t opcode = 0;
      const auto leftType = program.values[expr.args[0]].type;
      const auto rightType = program.values[expr.args[1]].type;
      uint32_t vectorOperand = left;
      uint32_t scalarOperand = right;
      if (expr.binaryOp == IRBinaryOp::Mul && IsFloatVector(leftType) &&
          rightType == SLType::Float) {
        opcode = spirv::kOpVectorTimesScalar;
      } else if (expr.binaryOp == IRBinaryOp::Mul && leftType == SLType::Float &&
                 IsFloatVector(rightType)) {
        opcode = spirv::kOpVectorTimesScalar;
        vectorOperand = right;
        scalarOperand = left;
      } else if (leftType == SLType::Float3x3 && rightType == SLType::Float3 &&
                 expr.binaryOp == IRBinaryOp::Mul) {
        opcode = spirv::kOpMatrixTimesVector;
      } else if (IsFloatVector(leftType) && IsFloatVector(rightType)) {
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
      if (opcode == spirv::kOpVectorTimesScalar) {
        writer.Inst(opcode, {type, id, vectorOperand, scalarOperand});
      } else {
        writer.Inst(opcode, {type, id, left, right});
      }
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
  if (irValue.kind == IRValueKind::Attribute) {
    const auto id = EnsureAttribute(program, irValue, error);
    if (id == 0) {
      return false;
    }
    valueIds.emplace(value, id);
    *resultId = id;
    return true;
  }
  if (irValue.kind == IRValueKind::Varying) {
    if (stage != ShaderStage::Fragment) {
      return Fail(error, "vertex varying outputs are store-only values");
    }
    const auto variable = EnsureVarying(program, irValue, error);
    const auto typeId = TypeId(irValue.type, error);
    if (variable == 0 || typeId == 0) {
      return false;
    }
    writer.Sink(&writer.func);
    const auto id = writer.NewId();
    writer.Inst(spirv::kOpLoad, {typeId, id, variable});
    valueIds.emplace(value, id);
    *resultId = id;
    return true;
  }
  if (irValue.kind == IRValueKind::Sampler) {
    // A linked operator call takes the sampler's descriptor slot itself (the UniformConstant
    // variable), not the loaded value: the imported module's parameter is a UniformConstant
    // pointer, and the texture-sample path keeps using its own loaded id.
    const auto variable = EnsureSamplerVariable(program, irValue, error);
    if (variable == 0) {
      return false;
    }
    samplerPointerArgs.insert(variable);
    valueIds.emplace(value, variable);
    *resultId = variable;
    return true;
  }
  if (irValue.definingExpr == INVALID_EXPR_ID || irValue.definingExpr >= program.exprs.size()) {
    return Fail(error, "the value has no defining expression (kind=" +
                           std::to_string(static_cast<int>(irValue.kind)) + ", name='" +
                           irValue.debugName + "')");
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
  return StoreOutput(program, INVALID_VALUE_ID, value, error);
}

bool SPIRVShaderBuilder::StoreOutput(const ProgramIR& program, ValueID dst, ValueID value,
                                     std::string* error) {
  if (!begun) {
    return Fail(error, "the SPIR-V shader builder has not begun");
  }
  const auto source = EmitValueAsValue(program, value, error);
  if (source == 0) {
    return false;
  }
  uint32_t target = outputPointerId;
  if (stage == ShaderStage::Fragment && dst != INVALID_VALUE_ID && dst < program.values.size() &&
      program.values[dst].debugName != "fragmentOutput" &&
      program.values[dst].debugName != "outputColor") {
    if (!EmitValue(program, dst, &target, error)) {
      return false;
    }
  }
  writer.Sink(&writer.func);
  writer.Inst(spirv::kOpStore, {target, source});
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
  EnsureLinkageCapability();
  for (const auto& existing : importedFunctions) {
    if (existing.symbol == artifact.symbol) {
      return existing.id;
    }
  }
  if (artifact.spirv.size() > 5 && artifact.spirv[0] == 0x07230203u) {
    importedModules.push_back(artifact.spirv);
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
  const ImportedFunction* imported = nullptr;
  for (const auto& candidate : importedFunctions) {
    if (candidate.id == functionId) {
      imported = &candidate;
      break;
    }
  }
  if (imported == nullptr || imported->paramTypeIds.size() != args.size()) {
    Fail(error, "the SPIR-V operator function arguments do not match its declaration");
    return 0;
  }
  writer.Sink(&writer.func);
  auto declareFunctionVariable = [&](uint32_t pointerType, uint32_t id) {
    size_t offset = 0;
    while (offset < writer.func.size()) {
      const auto instruction = writer.func[offset];
      const auto wordCount = instruction >> 16;
      const auto opcode = instruction & 0xFFFFu;
      if (wordCount == 0) {
        break;
      }
      if (opcode == spirv::kOpLabel) {
        const std::vector<uint32_t> variable = {(4u << 16) | spirv::kOpVariable, pointerType, id,
                                                spirv::kStorageFunction};
        writer.func.insert(writer.func.begin() + static_cast<ptrdiff_t>(offset + wordCount),
                           variable.begin(), variable.end());
        return;
      }
      offset += wordCount;
    }
  };
  std::vector<uint32_t> callArgs = {};
  for (size_t index = 0; index < args.size(); ++index) {
    if (samplerPointerArgs.count(args[index]) != 0) {
      // The sampler argument is the descriptor variable itself (a UniformConstant pointer) and
      // the imported parameter is exactly that type: it passes straight through, because staging
      // it through a Function-storage local would clash with the storage class.
      callArgs.push_back(args[index]);
      continue;
    }
    // Every other parameter is a Function-storage pointer while the argument is the plain value:
    // stage the value into a matching local and pass the local.
    const auto variableId = writer.NewId();
    declareFunctionVariable(imported->paramTypeIds[index], variableId);
    writer.Inst(spirv::kOpStore, {variableId, args[index]});
    callArgs.push_back(variableId);
  }
  const auto resultId = writer.NewId();
  std::vector<uint32_t> operands = {typeId, resultId, functionId};
  operands.insert(operands.end(), callArgs.begin(), callArgs.end());
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
#if defined(TGFX_USE_VULKAN)
  if (!importedModules.empty()) {
    std::vector<std::vector<uint32_t>> modules = {out};
    modules.insert(modules.end(), importedModules.begin(), importedModules.end());
    std::vector<uint32_t> linked = {};
    std::string linkError = {};
    if (!LinkChainSPIRV(modules, &linked, &linkError)) {
      Fail(error, linkError);
      return {};
    }
    out = std::move(linked);
  }
#endif
  finished = true;
  return out;
}

SPIRVFragmentShaderBuilder::SPIRVFragmentShaderBuilder()
    : SPIRVShaderBuilder(ShaderStage::Fragment) {
}

SPIRVVertexShaderBuilder::SPIRVVertexShaderBuilder() : SPIRVShaderBuilder(ShaderStage::Vertex) {
}

}  // namespace tgfx
