/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "VulkanEntryEmit.h"

#if defined(TGFX_USE_VULKAN) || defined(TGFX_USE_METAL)

#include <cstring>
#include <map>
#include <set>
#include <utility>
#include "gpu/aot/chain/SPIRVWriter.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {
namespace {

using namespace spirv;

struct SpirvModule {
  Writer writer = {};
  Types types = {};
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> pointers = {};
  std::map<uint32_t, uint32_t> intConstants = {};
  std::map<uint32_t, uint32_t> floatConstants = {};
  std::map<uint32_t, uint32_t> intVectors = {};

  uint32_t Int(int value) {
    const uint32_t key = static_cast<uint32_t>(value);
    const auto found = intConstants.find(key);
    if (found != intConstants.end()) {
      return found->second;
    }
    auto* sink = writer.sink;
    writer.Sink(&writer.decl);
    const uint32_t id = writer.NewId();
    writer.Inst(kOpConstant, {types.intId, id, key});
    writer.Sink(sink);
    intConstants[key] = id;
    return id;
  }

  uint32_t Float(float value) {
    uint32_t key = 0;
    std::memcpy(&key, &value, sizeof(key));
    const auto found = floatConstants.find(key);
    if (found != floatConstants.end()) {
      return found->second;
    }
    auto* sink = writer.sink;
    writer.Sink(&writer.decl);
    const uint32_t id = writer.NewId();
    writer.Inst(kOpConstant, {types.floatId, id, key});
    writer.Sink(sink);
    floatConstants[key] = id;
    return id;
  }

  uint32_t Pointer(uint32_t storageClass, uint32_t pointee) {
    const auto key = std::make_pair(storageClass, pointee);
    const auto found = pointers.find(key);
    if (found != pointers.end()) {
      return found->second;
    }
    auto* sink = writer.sink;
    writer.Sink(&writer.decl);
    const uint32_t id = writer.NewId();
    writer.Inst(kOpTypePointer, {id, storageClass, pointee});
    writer.Sink(sink);
    pointers[key] = id;
    return id;
  }

  uint32_t IntVector(uint32_t count) {
    const auto found = intVectors.find(count);
    if (found != intVectors.end()) {
      return found->second;
    }
    auto* sink = writer.sink;
    writer.Sink(&writer.decl);
    const uint32_t id = writer.NewId();
    writer.Inst(kOpTypeVector, {id, types.intId, count});
    writer.Sink(sink);
    intVectors[count] = id;
    return id;
  }
};

void DeclareCanonicalTypes(SpirvModule* module) {
  auto& writer = module->writer;
  auto& types = module->types;
  auto* sink = writer.sink;
  writer.Sink(&writer.decl);
  types.voidId = writer.NewId();
  writer.Inst(kOpTypeVoid, {types.voidId});
  types.boolId = writer.NewId();
  writer.Inst(kOpTypeBool, {types.boolId});
  types.intId = writer.NewId();
  writer.Inst(kOpTypeInt, {types.intId, 32, 1});
  types.floatId = writer.NewId();
  writer.Inst(kOpTypeFloat, {types.floatId, 32});
  types.vec2 = writer.NewId();
  writer.Inst(kOpTypeVector, {types.vec2, types.floatId, 2});
  types.vec3 = writer.NewId();
  writer.Inst(kOpTypeVector, {types.vec3, types.floatId, 3});
  types.vec4 = writer.NewId();
  writer.Inst(kOpTypeVector, {types.vec4, types.floatId, 4});
  types.ivec4 = writer.NewId();
  writer.Inst(kOpTypeVector, {types.ivec4, types.intId, 4});
  types.mat3 = writer.NewId();
  writer.Inst(kOpTypeMatrix, {types.mat3, types.vec3, 3});
  types.mat4 = writer.NewId();
  writer.Inst(kOpTypeMatrix, {types.mat4, types.vec4, 4});
  writer.Sink(sink);
}

uint32_t TypeOfShaderType(SpirvModule* module, ShaderType type, bool* ok) {
  const auto& types = module->types;
  switch (type) {
    case ShaderType::Float:
      return types.floatId;
    case ShaderType::Float2:
      return types.vec2;
    case ShaderType::Float3:
      return types.vec3;
    case ShaderType::Float4:
      return types.vec4;
    case ShaderType::Int:
      return types.intId;
    case ShaderType::Int4:
      return types.ivec4;
    default:
      break;
  }
  *ok = false;
  return 0;
}

uint32_t TypeOfFormat(SpirvModule* module, UniformFormat format, bool* ok) {
  const auto& types = module->types;
  switch (format) {
    case UniformFormat::Float:
      return types.floatId;
    case UniformFormat::Float2:
      return types.vec2;
    case UniformFormat::Float3:
      return types.vec3;
    case UniformFormat::Float4:
      return types.vec4;
    case UniformFormat::Int:
      return types.intId;
    case UniformFormat::Int2:
      return module->IntVector(2);
    case UniformFormat::Int3:
      return module->IntVector(3);
    case UniformFormat::Int4:
      return types.ivec4;
    case UniformFormat::Float3x3:
      return types.mat3;
    case UniformFormat::Float4x4:
      return types.mat4;
    default:
      break;
  }
  *ok = false;
  return 0;
}

// The std140 footprint one declared member occupies, mirroring the CPU-side block the upload
// writes through (UniformData): Float3 is 12 bytes aligned to 16, and matrices stride their
// columns by 16. The two must agree byte for byte or a draw reads another draw's parameters.
struct BlockEntry {
  uint32_t size = 0;
  uint32_t align = 0;
};

BlockEntry EntryOf(UniformFormat format) {
  switch (format) {
    case UniformFormat::Float:
      return {4, 4};
    case UniformFormat::Float2:
      return {8, 8};
    case UniformFormat::Float3:
      return {12, 16};
    case UniformFormat::Float4:
      return {16, 16};
    case UniformFormat::Float2x2:
      return {32, 16};
    case UniformFormat::Float3x3:
      return {48, 16};
    case UniformFormat::Float4x4:
      return {64, 16};
    case UniformFormat::Int:
      return {4, 4};
    case UniformFormat::Int2:
      return {8, 8};
    case UniformFormat::Int3:
      return {12, 16};
    case UniformFormat::Int4:
      return {16, 16};
    default:
      return {0, 0};
  }
}

uint32_t LoadValue(SpirvModule* module, uint32_t pointer, uint32_t type) {
  const uint32_t id = module->writer.NewId();
  module->writer.Inst(kOpLoad, {type, id, pointer});
  return id;
}

uint32_t Shuffle2(SpirvModule* module, uint32_t source, uint32_t first, uint32_t second) {
  const uint32_t id = module->writer.NewId();
  module->writer.Inst(kOpVectorShuffle, {module->types.vec2, id, source, source, first, second});
  return id;
}

uint32_t Compose4From2(SpirvModule* module, uint32_t pair, uint32_t third, uint32_t fourth) {
  const uint32_t id = module->writer.NewId();
  module->writer.Inst(kOpCompositeConstruct, {module->types.vec4, id, pair, third, fourth});
  return id;
}

void EmitPreamble(SpirvModule* module, bool needsGlslStd450, uint32_t* glslSet) {
  auto& writer = module->writer;
  writer.Inst(kOpCapability, {kCapabilityShader});
  if (needsGlslStd450) {
    *glslSet = writer.NewId();
    std::vector<uint32_t> operands = {*glslSet};
    const auto nameWords = NameWords("GLSL.std.450");
    operands.insert(operands.end(), nameWords.begin(), nameWords.end());
    writer.Inst(kOpExtInstImport, operands);
  }
  writer.Inst(kOpMemoryModel, {0, 1});  // Logical, GLSL450
  DeclareCanonicalTypes(module);
}

// One varying the vertex stage writes and the fragment stage reads.
struct VaryingSlot {
  uint32_t variable = 0;
  uint32_t type = 0;
};

}  // namespace

bool EmitVulkanFragmentFrame(const ChainProgramPlan& plan, const Layout& layout,
                             std::vector<uint32_t>* out, std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  if (plan.varyings.size() != layout.varyings.size()) {
    *error = "the frame's plan and layout disagree about the varying count";
    return false;
  }
  SpirvModule module = {};
  auto& writer = module.writer;
  auto& types = module.types;
  EmitPreamble(&module, false, nullptr);

  // The fragment coordinate: the frame reads gl_FragCoord and hands its xy to chainBody.
  writer.Sink(&writer.decl);
  const uint32_t fragCoordPointer = module.Pointer(kStorageInput, types.vec4);
  const uint32_t fragCoordVar = writer.NewId();
  writer.Inst(kOpVariable, {fragCoordPointer, fragCoordVar, kStorageInput});
  writer.Sink(&writer.ann);
  writer.Inst(kOpDecorate, {fragCoordVar, kDecorationBuiltIn, kBuiltInFragCoord});

  // One input per declared varying, at the layout's own numbering.
  std::vector<VaryingSlot> slots = {};
  for (size_t index = 0; index < layout.varyings.size(); ++index) {
    bool ok = true;
    const uint32_t type = TypeOfShaderType(&module, layout.varyings[index].type, &ok);
    if (!ok) {
      *error = "the layout declares a varying the frame cannot carry";
      return false;
    }
    writer.Sink(&writer.decl);
    const uint32_t pointer = module.Pointer(kStorageInput, type);
    const uint32_t variable = writer.NewId();
    writer.Inst(kOpVariable, {pointer, variable, kStorageInput});
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate, {variable, kDecorationLocation, static_cast<uint32_t>(index)});
    slots.push_back({variable, type});
  }

  // The single color output the driver reads.
  writer.Sink(&writer.decl);
  const uint32_t outputPointer = module.Pointer(kStorageOutput, types.vec4);
  const uint32_t outputVar = writer.NewId();
  writer.Inst(kOpVariable, {outputPointer, outputVar, kStorageOutput});
  writer.Sink(&writer.ann);
  writer.Inst(kOpDecorate, {outputVar, kDecorationLocation, 0});

  // The entry point, with every input and output it uses on its interface.
  std::vector<uint32_t> interfaceIds = {fragCoordVar, outputVar};
  for (const auto& slot : slots) {
    interfaceIds.push_back(slot.variable);
  }
  writer.Sink(&writer.mode);
  {
    std::vector<uint32_t> operands = {static_cast<uint32_t>(kExecutionModelFragment)};
    const uint32_t mainId = writer.NewId();
    operands.push_back(mainId);
    const auto name = NameWords("main");
    operands.insert(operands.end(), name.begin(), name.end());
    operands.insert(operands.end(), interfaceIds.begin(), interfaceIds.end());
    writer.Inst(kOpEntryPoint, operands);
    writer.Inst(kOpExecutionMode, {mainId, kExecutionModeOriginUpperLeft});

    // chainBody's import: the same parameter list the body derives from the layout.
    std::vector<uint32_t> parameterTypes = {module.Pointer(kStorageFunction, types.vec2)};
    for (const auto& slot : slots) {
      parameterTypes.push_back(module.Pointer(kStorageFunction, slot.type));
    }
    writer.Sink(&writer.decl);
    const uint32_t functionType = writer.NewId();
    {
      std::vector<uint32_t> operands = {functionType, types.vec4};
      operands.insert(operands.end(), parameterTypes.begin(), parameterTypes.end());
      writer.Inst(kOpTypeFunction, operands);
    }
    const uint32_t bodyId = writer.NewId();
    writer.Sink(&writer.ann);
    {
      std::vector<uint32_t> operands = {bodyId, kDecorationLinkageAttributes};
      const auto nameWords = NameWords("chainBody");
      operands.insert(operands.end(), nameWords.begin(), nameWords.end());
      operands.push_back(kLinkageTypeImport);
      writer.Inst(kOpDecorate, operands);
    }
    writer.Sink(&writer.func);
    writer.Inst(kOpFunction, {types.vec4, bodyId, 0, functionType});
    for (uint32_t parameterType : parameterTypes) {
      writer.Inst(kOpFunctionParameter, {parameterType, writer.NewId()});
    }
    writer.Inst(kOpFunctionEnd);

    const uint32_t mainType = writer.NewId();
    writer.Sink(&writer.decl);
    writer.Inst(kOpTypeFunction, {mainType, types.voidId});

    writer.Sink(&writer.func);
    writer.Inst(kOpFunction, {types.voidId, mainId, 0, mainType});
    writer.Inst(kOpLabel, {writer.NewId()});
    // Every function-scope variable precedes the block's other instructions.
    const uint32_t coordLocal = writer.NewId();
    writer.Inst(kOpVariable,
                {module.Pointer(kStorageFunction, types.vec2), coordLocal, kStorageFunction});
    std::vector<uint32_t> argumentLocals = {};
    for (const auto& slot : slots) {
      const uint32_t local = writer.NewId();
      writer.Inst(kOpVariable,
                  {module.Pointer(kStorageFunction, slot.type), local, kStorageFunction});
      argumentLocals.push_back(local);
    }
    const uint32_t coordValue = LoadValue(&module, fragCoordVar, types.vec4);
    writer.Inst(kOpStore, {coordLocal, Shuffle2(&module, coordValue, 0, 1)});
    for (size_t index = 0; index < slots.size(); ++index) {
      const uint32_t value = LoadValue(&module, slots[index].variable, slots[index].type);
      writer.Inst(kOpStore, {argumentLocals[index], value});
    }
    std::vector<uint32_t> callOperands = {types.vec4, writer.NewId(), bodyId, coordLocal};
    callOperands.insert(callOperands.end(), argumentLocals.begin(), argumentLocals.end());
    writer.Inst(kOpFunctionCall, callOperands);
    writer.Inst(kOpStore, {outputVar, callOperands[1]});
    writer.Inst(kOpReturn);
    writer.Inst(kOpFunctionEnd);
  }
  writer.Finish(out);
  return true;
}

bool EmitVulkanVertexBody(const std::vector<VertexCallRecord>& calls, const Layout& layout,
                          const std::vector<Uniform>& uniforms, std::vector<uint32_t>* out,
                          std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  SpirvModule module = {};
  auto& writer = module.writer;
  auto& types = module.types;
  uint32_t glslSet = 0;
  EmitPreamble(&module, true, &glslSet);

  // The attributes at their layout locations.
  std::map<std::string, VaryingSlot> attributeOf = {};
  for (const auto& attribute : layout.attributes) {
    bool ok = true;
    const uint32_t type = TypeOfFormat(&module, attribute.format, &ok);
    if (!ok) {
      *error = "the layout declares an attribute the vertex stage cannot carry";
      return false;
    }
    writer.Sink(&writer.decl);
    const uint32_t pointer = module.Pointer(kStorageInput, type);
    const uint32_t variable = writer.NewId();
    writer.Inst(kOpVariable, {pointer, variable, kStorageInput});
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate,
                {variable, kDecorationLocation, static_cast<uint32_t>(attribute.location)});
    attributeOf[attribute.name] = {variable, type};
  }

  // The varyings the fragment stage reads, numbered by declaration order on both stages.
  std::map<std::string, VaryingSlot> varyingOf = {};
  for (size_t index = 0; index < layout.varyings.size(); ++index) {
    bool ok = true;
    const uint32_t type = TypeOfShaderType(&module, layout.varyings[index].type, &ok);
    if (!ok) {
      *error = "the layout declares a varying the vertex stage cannot carry";
      return false;
    }
    writer.Sink(&writer.decl);
    const uint32_t pointer = module.Pointer(kStorageOutput, type);
    const uint32_t variable = writer.NewId();
    writer.Inst(kOpVariable, {pointer, variable, kStorageOutput});
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate, {variable, kDecorationLocation, static_cast<uint32_t>(index)});
    varyingOf[layout.varyings[index].name] = {variable, type};
  }

  writer.Sink(&writer.decl);
  const uint32_t positionPointer = module.Pointer(kStorageOutput, types.vec4);
  const uint32_t positionVar = writer.NewId();
  writer.Inst(kOpVariable, {positionPointer, positionVar, kStorageOutput});
  writer.Sink(&writer.ann);
  writer.Inst(kOpDecorate, {positionVar, kDecorationBuiltIn, kBuiltInPosition});

  // The vertex uniform block: the declared members at the CPU-side block's own offsets.
  std::map<std::string, std::pair<int, uint32_t>> memberOf = {};
  uint32_t structId = 0;
  uint32_t uboId = 0;
  if (!uniforms.empty()) {
    writer.Sink(&writer.decl);
    structId = writer.NewId();
    std::vector<uint32_t> memberTypes = {};
    std::vector<uint32_t> offsets = {};
    uint32_t cursor = 0;
    for (const auto& uniform : uniforms) {
      bool ok = true;
      const uint32_t type = TypeOfFormat(&module, uniform.format(), &ok);
      if (!ok) {
        *error = "the vertex stage declares a member the block cannot carry";
        return false;
      }
      const BlockEntry entry = EntryOf(uniform.format());
      if (entry.align == 0) {
        *error = "the vertex stage declares a member the block cannot size";
        return false;
      }
      cursor = (cursor + entry.align - 1) / entry.align * entry.align;
      offsets.push_back(cursor);
      cursor += entry.size * uniform.count();
      memberOf[uniform.name()] = {static_cast<int>(memberTypes.size()), type};
      if (uniform.count() > 1) {
        const uint32_t stride = uniform.format() == UniformFormat::Float4x4 ? 64u : 16u;
        const uint32_t arrayType = writer.NewId();
        writer.Inst(kOpTypeArray, {arrayType, type, module.Int(static_cast<int>(uniform.count()))});
        writer.Sink(&writer.ann);
        writer.Inst(kOpDecorate, {arrayType, kDecorationArrayStride, stride});
        writer.Sink(&writer.decl);
        memberTypes.push_back(arrayType);
      } else {
        memberTypes.push_back(type);
      }
    }
    {
      std::vector<uint32_t> operands = {structId};
      operands.insert(operands.end(), memberTypes.begin(), memberTypes.end());
      writer.Inst(kOpTypeStruct, operands);
    }
    writer.Sink(&writer.debug);
    for (size_t index = 0; index < uniforms.size(); ++index) {
      std::vector<uint32_t> operands = {structId, static_cast<uint32_t>(index)};
      const auto nameWords = NameWords(uniforms[index].name().c_str());
      operands.insert(operands.end(), nameWords.begin(), nameWords.end());
      writer.Inst(kOpMemberName, operands);
    }
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate, {structId, kDecorationBlock});
    for (size_t index = 0; index < uniforms.size(); ++index) {
      writer.Inst(kOpMemberDecorate,
                  {structId, static_cast<uint32_t>(index), kDecorationOffset, offsets[index]});
      const auto format = uniforms[index].format();
      if (format == UniformFormat::Float3x3 || format == UniformFormat::Float4x4) {
        writer.Inst(kOpMemberDecorate,
                    {structId, static_cast<uint32_t>(index), kDecorationMatrixStride, 16});
        writer.Inst(kOpMemberDecorate,
                    {structId, static_cast<uint32_t>(index), kDecorationColMajor});
      }
    }
    writer.Sink(&writer.decl);
    const uint32_t uboPointer = module.Pointer(kStorageUniform, structId);
    uboId = writer.NewId();
    writer.Inst(kOpVariable, {uboPointer, uboId, kStorageUniform});
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate, {uboId, kDecorationDescriptorSet, VERTEX_UBO_DESCRIPTOR_SET});
    writer.Inst(kOpDecorate, {uboId, kDecorationBinding, VERTEX_UBO_BINDING_POINT});
  }

  // The entry point.
  std::vector<uint32_t> interfaceIds = {positionVar};
  for (const auto& entry : attributeOf) {
    interfaceIds.push_back(entry.second.variable);
  }
  for (const auto& entry : varyingOf) {
    interfaceIds.push_back(entry.second.variable);
  }
  writer.Sink(&writer.mode);
  const uint32_t mainId = writer.NewId();
  {
    std::vector<uint32_t> operands = {static_cast<uint32_t>(kExecutionModelVertex), mainId};
    const auto nameWords = NameWords("main");
    operands.insert(operands.end(), nameWords.begin(), nameWords.end());
    operands.insert(operands.end(), interfaceIds.begin(), interfaceIds.end());
    writer.Inst(kOpEntryPoint, operands);
  }
  const uint32_t mainType = writer.NewId();
  writer.Sink(&writer.decl);
  writer.Inst(kOpTypeFunction, {mainType, types.voidId});

  // The recorded writes replayed. Values are SSA ids by declaration name; an attribute load or a
  // block member load happens once, on first use.
  std::map<std::string, uint32_t> values = {};
  std::set<std::string> storedVaryings = {};
  auto valueOf = [&](const std::string& name, uint32_t* value) -> bool {
    const auto local = values.find(name);
    if (local != values.end()) {
      *value = local->second;
      return true;
    }
    const auto attribute = attributeOf.find(name);
    if (attribute != attributeOf.end()) {
      const uint32_t loaded =
          LoadValue(&module, attribute->second.variable, attribute->second.type);
      values[name] = loaded;
      *value = loaded;
      return true;
    }
    const auto member = memberOf.find(name);
    if (member != memberOf.end()) {
      if (uboId == 0) {
        return false;
      }
      const uint32_t pointerType = module.Pointer(kStorageUniform, member->second.second);
      const uint32_t chainId = writer.NewId();
      writer.Inst(kOpAccessChain, {pointerType, chainId, uboId, module.Int(member->second.first)});
      const uint32_t loaded = LoadValue(&module, chainId, member->second.second);
      values[name] = loaded;
      *value = loaded;
      return true;
    }
    return false;
  };
  auto loadVarying = [&](const std::string& name, VaryingSlot* slot) -> bool {
    const auto found = varyingOf.find(name);
    if (found == varyingOf.end()) {
      return false;
    }
    *slot = found->second;
    return true;
  };
  // One 2D point transformed by the named mat3: the shared form every geometry uses.
  auto transform = [&](const std::string& srcName, const std::string& matrixName,
                       bool hasPerspective, uint32_t* xy) -> bool {
    uint32_t source = 0;
    uint32_t matrix = 0;
    if (!valueOf(srcName, &source) || !valueOf(matrixName, &matrix)) {
      return false;
    }
    const uint32_t vector = writer.NewId();
    writer.Inst(kOpCompositeConstruct, {types.vec3, vector, source, module.Float(1.0f)});
    const uint32_t transformed = writer.NewId();
    writer.Inst(kOpMatrixTimesVector, {types.vec3, transformed, matrix, vector});
    if (!hasPerspective) {
      *xy = Shuffle2(&module, transformed, 0, 1);
      return true;
    }
    const uint32_t z = writer.NewId();
    writer.Inst(kOpCompositeExtract, {types.floatId, z, transformed, 2});
    const uint32_t xyValue = Shuffle2(&module, transformed, 0, 1);
    const uint32_t divided = writer.NewId();
    writer.Inst(kOpFDiv, {types.vec2, divided, xyValue, z});
    *xy = divided;
    return true;
  };

  writer.Sink(&writer.func);
  writer.Inst(kOpFunction, {types.voidId, mainId, 0, mainType});
  writer.Inst(kOpLabel, {writer.NewId()});
  bool positionWritten = false;
  for (const auto& call : calls) {
    switch (call.kind) {
      case VertexCallKind::StoreVarying: {
        VaryingSlot slot = {};
        uint32_t value = 0;
        if (!loadVarying(call.dst.name, &slot) || !valueOf(call.src.name, &value)) {
          *error = "a varying store reads " + call.src.name + " which the stage cannot resolve";
          return false;
        }
        writer.Inst(kOpStore, {slot.variable, value});
        storedVaryings.insert(call.dst.name);
        break;
      }
      case VertexCallKind::TransformPoint:
      case VertexCallKind::TransformVarying: {
        uint32_t xy = 0;
        if (!transform(call.src.name, call.operand.name, call.hasPerspective, &xy)) {
          *error = "a point transform reads " + call.src.name + "/" + call.operand.name +
                   " which the stage cannot resolve";
          return false;
        }
        if (call.kind == VertexCallKind::TransformPoint) {
          values[call.dst.name] = xy;
          break;
        }
        VaryingSlot slot = {};
        if (!loadVarying(call.dst.name, &slot)) {
          *error = "a transform writes an undeclared varying " + call.dst.name;
          return false;
        }
        writer.Inst(kOpStore, {slot.variable, xy});
        storedVaryings.insert(call.dst.name);
        break;
      }
      case VertexCallKind::TransformPointAdd: {
        uint32_t xy = 0;
        uint32_t addend = 0;
        if (!transform(call.src.name, call.operand.name, false, &xy) ||
            !valueOf(call.extra.name, &addend)) {
          *error = "a shifted transform reads " + call.src.name + "/" + call.extra.name +
                   " which the stage cannot resolve";
          return false;
        }
        const uint32_t sum = writer.NewId();
        writer.Inst(kOpFAdd, {types.vec2, sum, xy, addend});
        values[call.dst.name] = sum;
        break;
      }
      case VertexCallKind::MultiplyVec2: {
        uint32_t first = 0;
        uint32_t second = 0;
        if (!valueOf(call.src.name, &first) || !valueOf(call.operand.name, &second)) {
          *error = "a multiply reads " + call.src.name + "/" + call.operand.name +
                   " which the stage cannot resolve";
          return false;
        }
        const uint32_t product = writer.NewId();
        writer.Inst(kOpFMul, {types.vec2, product, first, second});
        values[call.dst.name] = product;
        break;
      }
      case VertexCallKind::SwizzleVec2: {
        uint32_t source = 0;
        if (!valueOf(call.src.name, &source)) {
          *error = "a swizzle reads " + call.src.name + " which the stage cannot resolve";
          return false;
        }
        const uint32_t first = call.extra.name == "zw" ? 2 : 0;
        values[call.dst.name] = Shuffle2(&module, source, first, first + 1);
        break;
      }
      case VertexCallKind::ComposeVec4: {
        uint32_t first = 0;
        uint32_t second = 0;
        if (!valueOf(call.src.name, &first) || !valueOf(call.operand.name, &second)) {
          *error = "a vec4 compose reads " + call.src.name + "/" + call.operand.name +
                   " which the stage cannot resolve";
          return false;
        }
        const uint32_t id = writer.NewId();
        writer.Inst(kOpCompositeConstruct, {types.vec4, id, first, second});
        values[call.dst.name] = id;
        break;
      }
      case VertexCallKind::NormalizeRect: {
        uint32_t source = 0;
        if (!valueOf(call.src.name, &source)) {
          *error =
              "a rect normalization reads " + call.src.name + " which the stage cannot resolve";
          return false;
        }
        const uint32_t low = Shuffle2(&module, source, 0, 1);
        const uint32_t high = Shuffle2(&module, source, 2, 3);
        const uint32_t minimum = writer.NewId();
        writer.Inst(kOpExtInst, {types.vec2, minimum, glslSet, kGLSLStd450FMin, low, high});
        const uint32_t maximum = writer.NewId();
        writer.Inst(kOpExtInst, {types.vec2, maximum, glslSet, kGLSLStd450FMax, low, high});
        const uint32_t id = writer.NewId();
        writer.Inst(kOpCompositeConstruct, {types.vec4, id, minimum, maximum});
        values[call.src.name] = id;
        break;
      }
      case VertexCallKind::StorePosition: {
        uint32_t source = 0;
        uint32_t adjust = 0;
        if (!valueOf(call.src.name, &source) || !valueOf(RTAdjustName, &adjust)) {
          *error = "the clip-space write cannot resolve " + call.src.name + " or " + RTAdjustName;
          return false;
        }
        const uint32_t scale = Shuffle2(&module, adjust, 0, 2);
        const uint32_t offset = Shuffle2(&module, adjust, 1, 3);
        const uint32_t scaled = writer.NewId();
        writer.Inst(kOpFMul, {types.vec2, scaled, source, scale});
        const uint32_t shifted = writer.NewId();
        writer.Inst(kOpFAdd, {types.vec2, shifted, scaled, offset});
        writer.Inst(kOpStore, {positionVar, Compose4From2(&module, shifted, module.Float(0.0f),
                                                          module.Float(1.0f))});
        positionWritten = true;
        break;
      }
    }
  }
  if (!positionWritten) {
    *error = "the vertex writes never reached the clip-space position";
    return false;
  }
  for (const auto& varying : layout.varyings) {
    if (storedVaryings.find(varying.name) == storedVaryings.end()) {
      *error = "the vertex stage never produced the varying " + varying.name;
      return false;
    }
  }
  writer.Inst(kOpReturn);
  writer.Inst(kOpFunctionEnd);
  writer.Finish(out);
  return true;
}

}  // namespace tgfx

#else  // TGFX_USE_VULKAN || TGFX_USE_METAL

namespace tgfx {

bool EmitVulkanFragmentFrame(const ChainProgramPlan&, const Layout&, std::vector<uint32_t>*,
                             std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan entry route is not part of this build";
  }
  return false;
}

bool EmitVulkanVertexBody(const std::vector<VertexCallRecord>&, const Layout&,
                          const std::vector<Uniform>&, std::vector<uint32_t>*, std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan entry route is not part of this build";
  }
  return false;
}

}  // namespace tgfx

#endif  // TGFX_USE_VULKAN || TGFX_USE_METAL
