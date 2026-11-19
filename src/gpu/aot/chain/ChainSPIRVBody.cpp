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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainSPIRVBody.h"
// The linking route exists only where Vulkan does: the SPIRV-Tools sources the runtime links
// against are built for the Vulkan configuration alone. Other backends get the declining stubs
// below, so the creator's Vulkan branch collapses to its fallback.
#if defined(TGFX_USE_VULKAN)

#include <cstring>
#include <unordered_map>
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "spirv-tools/linker.hpp"

namespace tgfx {
namespace {

// The opcodes, decorations and enumerants the emitter touches.
enum : uint16_t {
  kOpCapability = 17,
  kOpMemoryModel = 14,
  kOpTypeVoid = 19,
  kOpTypeBool = 20,
  kOpTypeInt = 21,
  kOpTypeFloat = 22,
  kOpTypeVector = 23,
  kOpTypeMatrix = 24,
  kOpTypeArray = 28,
  kOpTypeStruct = 30,
  kOpTypePointer = 32,
  kOpTypeFunction = 33,
  kOpConstant = 43,
  kOpConstantComposite = 44,
  kOpFunction = 54,
  kOpFunctionParameter = 55,
  kOpFunctionEnd = 56,
  kOpFunctionCall = 57,
  kOpVariable = 59,
  kOpLoad = 61,
  kOpStore = 62,
  kOpAccessChain = 65,
  kOpDecorate = 71,
  kOpMemberDecorate = 72,
  kOpVectorShuffle = 79,
  kOpCompositeConstruct = 80,
  kOpCompositeExtract = 81,
  kOpTypeImage = 25,
  kOpTypeSampler = 26,
  kOpTypeSampledImage = 27,
  kOpMatrixTimesVector = 145,
  kOpImageSampleImplicitLod = 87,
  kOpFMul = 133,
  kOpINotEqual = 171,
  kOpSelect = 169,
  kOpLabel = 248,
  kOpReturnValue = 254,
};
enum : uint32_t {
  kDecorationBlock = 2,
  kDecorationArrayStride = 6,
  kDecorationBinding = 33,
  kDecorationDescriptorSet = 34,
  kDecorationOffset = 35,
  kDecorationLinkageAttributes = 41,
};
enum : uint32_t { kCapabilityShader = 1, kCapabilityLinkage = 5 };
enum : uint32_t { kLinkageTypeExport = 0, kLinkageTypeImport = 1 };
enum : uint32_t { kStorageUniform = 2, kStorageUniformConstant = 0, kStorageFunction = 7 };

// The module's logical sections: SPIR-V requires capabilities, then annotations, then types and
// constants, then functions — instructions must appear in that order or no validator accepts the
// module. The writer routes each instruction into its section as it is emitted, so the emission
// code can interleave types and decorations freely.
struct Writer {
  std::vector<uint32_t> pre = {};   // capabilities and the memory model
  std::vector<uint32_t> ann = {};   // decorations
  std::vector<uint32_t> decl = {};  // types, constants, variables
  std::vector<uint32_t> func = {};  // functions
  std::vector<uint32_t>* sink = &pre;
  uint32_t nextId = 1;

  uint32_t NewId() {
    return nextId++;
  }

  void Sink(std::vector<uint32_t>* section) {
    sink = section;
  }

  void Inst(uint16_t opcode, const std::vector<uint32_t>& operands) {
    sink->push_back((static_cast<uint32_t>(operands.size() + 1) << 16) | opcode);
    sink->insert(sink->end(), operands.begin(), operands.end());
  }

  void Inst(uint16_t opcode) {
    Inst(opcode, {});
  }

  void Finish(std::vector<uint32_t>* out) const {
    out->clear();
    out->reserve(5 + pre.size() + ann.size() + decl.size() + func.size());
    out->push_back(0x07230203u);
    out->push_back(0x00010000u);  // SPIR-V 1.0, the op modules' version
    out->push_back(0);
    out->push_back(nextId);
    out->push_back(0);
    out->insert(out->end(), pre.begin(), pre.end());
    out->insert(out->end(), ann.begin(), ann.end());
    out->insert(out->end(), decl.begin(), decl.end());
    out->insert(out->end(), func.begin(), func.end());
  }
};

// The canonical type ids, created once per module.
struct Types {
  uint32_t voidId = 0;
  uint32_t boolId = 0;
  uint32_t intId = 0;
  uint32_t floatId = 0;
  uint32_t vec2 = 0;
  uint32_t vec3 = 0;
  uint32_t vec4 = 0;
  uint32_t ivec4 = 0;
  uint32_t mat3 = 0;
  uint32_t mat4 = 0;
  uint32_t image = 0;
  uint32_t samplerType = 0;
  uint32_t sampledImage = 0;
};

uint32_t VaryingTypeId(const Types& types, ShaderType type, bool* ok) {
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

uint32_t MemberTypeId(const Types& types, FragmentType type, bool* ok) {
  switch (type) {
    case FragmentType::Float:
      return types.floatId;
    case FragmentType::Float2:
      return types.vec2;
    case FragmentType::Float3:
      return types.vec3;
    case FragmentType::Float4:
      return types.vec4;
    case FragmentType::Int:
      return types.intId;
    case FragmentType::Int4:
      return types.ivec4;
    case FragmentType::Matrix3:
      return types.mat3;
    case FragmentType::Matrix4:
      return types.mat4;
  }
  *ok = false;
  return 0;
}

// The operator signatures the pointwise slice links: parameter type kinds in call order, the
// result is always vec4. The names are the export strings the build's op modules carry.
enum ParamKind : uint8_t { kFloat = 0, kInt, kVec2, kVec3, kVec4, kMat3, kMat4 };

struct OpSignature {
  int op = 0;
  const char* name = nullptr;
  std::vector<ParamKind> params = {};
};

const std::vector<OpSignature>& OpSignatures() {
  static const std::vector<OpSignature> signatures = {
      {ChainISA::ConstColor, "pmApplyConstColor", {kVec4, kVec4, kInt}},
      {ChainISA::ColorMatrix, "pmApplyColorMatrix", {kVec4, kMat4, kVec4}},
      {ChainISA::Luma, "pmApplyLuma", {kVec4, kVec3}},
      {ChainISA::AlphaThreshold, "pmApplyAlphaThreshold", {kVec4, kFloat}},
      {ChainISA::Blend, "xpBlendColors", {kVec4, kVec4, kInt}},
      {ChainISA::AARectCoverage, "tgfxOpAARectCoverage", {kVec4, kVec4, kVec2}},
      {ChainISA::LocalRectCoverage, "tgfxOpLocalRectCoverage", {kVec4, kVec4, kMat3, kVec2}},
      {ChainISA::RRectCoverage,
       "tgfxOpRRectCoverage",
       {kVec4, kVec4, kVec4, kVec4, kFloat, kMat3, kVec2}},
  };
  return signatures;
}

const OpSignature* FindSignature(int op) {
  for (const auto& signature : OpSignatures()) {
    if (signature.op == op) {
      return &signature;
    }
  }
  return nullptr;
}

uint32_t ParamTypeId(const Types& types, ParamKind kind) {
  switch (kind) {
    case kFloat:
      return types.floatId;
    case kInt:
      return types.intId;
    case kVec2:
      return types.vec2;
    case kVec3:
      return types.vec3;
    case kVec4:
      return types.vec4;
    case kMat3:
      return types.mat3;
    case kMat4:
      return types.mat4;
  }
  return types.floatId;
}

// One planned call resolved into the module's ids.
struct BodyState {
  Writer writer = {};
  Types types = {};
  // The chainBody parameter ids in the frame's declared order: fragCoord first, then the
  // variant's varyings.
  uint32_t fragCoordId = 0;
  std::unordered_map<std::string, uint32_t> varyingIds = {};
  // The struct and its variable.
  uint32_t structId = 0;
  uint32_t uboId = 0;
  // Member loads by plan.uniforms index, created once per member.
  std::unordered_map<int, uint32_t> memberLoads = {};
  // Register -> value id.
  std::unordered_map<int, uint32_t> registers = {};
  // Cached constants.
  std::unordered_map<uint32_t, uint32_t> intConstants = {};
  uint32_t whiteId = 0;
  uint32_t blackId = 0;
  uint32_t oneFloatId = 0;
  uint32_t geomColorId = 0;
  uint32_t opaqueColorId = 0;
  // The op function ids by ChainISA value.
  std::unordered_map<int, uint32_t> opFunctions = {};
  // The Pointer(Function, T) types, cached per pointee: glslang passes every non-entry
  // function's parameters as pointers to Function storage, and the linked modules keep that ABI.
  std::unordered_map<uint32_t, uint32_t> functionPointers = {};
  std::unordered_map<uint32_t, uint32_t> uniformConstantPointers = {};
};

uint32_t PtrTo(BodyState* state, uint32_t pointee) {
  const auto found = state->functionPointers.find(pointee);
  if (found != state->functionPointers.end()) {
    return found->second;
  }
  // Types belong to the declaration section even when they are created lazily from the body.
  auto* sink = state->writer.sink;
  state->writer.Sink(&state->writer.decl);
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpTypePointer, {id, kStorageFunction, pointee});
  state->writer.Sink(sink);
  state->functionPointers[pointee] = id;
  return id;
}

// Sampler-ish parameters live behind a UniformConstant pointer (glslang's convention for opaque
// types); everything else is a Function pointer.
uint32_t UniformConstantPtrTo(BodyState* state, uint32_t pointee) {
  const auto found = state->uniformConstantPointers.find(pointee);
  if (found != state->uniformConstantPointers.end()) {
    return found->second;
  }
  auto* sink = state->writer.sink;
  state->writer.Sink(&state->writer.decl);
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpTypePointer, {id, kStorageUniformConstant, pointee});
  state->writer.Sink(sink);
  state->uniformConstantPointers[pointee] = id;
  return id;
}

uint32_t LoadFrom(BodyState* state, uint32_t pointer, uint32_t type) {
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpLoad, {type, id, pointer});
  return id;
}

uint32_t IntConstant(BodyState* state, int value) {
  const uint32_t bits = static_cast<uint32_t>(value);
  const auto found = state->intConstants.find(bits);
  if (found != state->intConstants.end()) {
    return found->second;
  }
  auto* sink = state->writer.sink;
  state->writer.Sink(&state->writer.decl);
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpConstant, {state->types.intId, id, bits});
  state->writer.Sink(sink);
  state->intConstants[bits] = id;
  return id;
}

uint32_t FloatConstant(BodyState* state, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  auto* sink = state->writer.sink;
  state->writer.Sink(&state->writer.decl);
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpConstant, {state->types.floatId, id, bits});
  state->writer.Sink(sink);
  return id;
}

uint32_t Splat4(BodyState* state, uint32_t scalar) {
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpCompositeConstruct,
                     {state->types.vec4, id, scalar, scalar, scalar, scalar});
  return id;
}

// The loaded value of one plan uniform member (cached per member).
uint32_t LoadMember(BodyState* state, const ChainProgramPlan& plan, int memberIndex) {
  const auto found = state->memberLoads.find(memberIndex);
  if (found != state->memberLoads.end()) {
    return found->second;
  }
  const auto& member = plan.uniforms[static_cast<size_t>(memberIndex)];
  bool ok = true;
  uint32_t typeId = MemberTypeId(state->types, member.type, &ok);
  if (!ok) {
    return 0;
  }
  if (member.count > 1) {
    // Array members are not part of the v1 slice; the gate keeps them out.
    return 0;
  }
  const uint32_t pointerType = state->writer.NewId();
  {
    // The Uniform pointer type is a type too: declaration section regardless of the caller.
    auto* sink = state->writer.sink;
    state->writer.Sink(&state->writer.decl);
    state->writer.Inst(kOpTypePointer, {pointerType, kStorageUniform, typeId});
    state->writer.Sink(sink);
  }
  const uint32_t chainId = state->writer.NewId();
  state->writer.Inst(kOpAccessChain,
                     {pointerType, chainId, state->uboId, IntConstant(state, memberIndex)});
  const uint32_t valueId = state->writer.NewId();
  state->writer.Inst(kOpLoad, {typeId, valueId, chainId});
  state->memberLoads[memberIndex] = valueId;
  return valueId;
}

// The member index of one binding source, or -1.
int MemberIndexOf(const ChainProgramPlan& plan, ChainBindingSource source) {
  for (size_t index = 0; index < plan.uniforms.size(); ++index) {
    if (plan.uniforms[index].source == source) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

// The geometry color: the common color when the variant carries one, the vColor varying
// otherwise (mirrors the text route's chainGeomColorSource).
uint32_t GeomColor(BodyState* state, const ChainProgramPlan& plan) {
  if (state->geomColorId != 0) {
    return state->geomColorId;
  }
  const int colorIndex = MemberIndexOf(plan, ChainBindingSource::GeometryColor);
  const int hasColorIndex = MemberIndexOf(plan, ChainBindingSource::GeometryHasCommonColor);
  const auto vColor = state->varyingIds.find("vColor");
  if (colorIndex < 0 || hasColorIndex < 0 || vColor == state->varyingIds.end()) {
    return 0;
  }
  const uint32_t colorId = LoadMember(state, plan, colorIndex);
  const uint32_t hasColorId = LoadMember(state, plan, hasColorIndex);
  const uint32_t condition = state->writer.NewId();
  state->writer.Inst(kOpINotEqual,
                     {state->types.boolId, condition, hasColorId, IntConstant(state, 0)});
  const uint32_t picked = state->writer.NewId();
  state->writer.Inst(kOpSelect, {state->types.vec4, picked, condition, colorId, vColor->second});
  state->geomColorId = picked;
  return picked;
}

// vec4(geom.rgb, 1.0).
uint32_t OpaqueColor(BodyState* state, const ChainProgramPlan& plan) {
  if (state->opaqueColorId != 0) {
    return state->opaqueColorId;
  }
  const uint32_t geom = GeomColor(state, plan);
  if (geom == 0) {
    return 0;
  }
  const uint32_t rgb = state->writer.NewId();
  state->writer.Inst(kOpVectorShuffle, {state->types.vec3, rgb, geom, geom, 0, 1, 2});
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpCompositeConstruct, {state->types.vec4, id, rgb, state->oneFloatId});
  state->opaqueColorId = id;
  return id;
}

// Resolves one plan argument to a value id; 0 on failure.
uint32_t ResolveArg(BodyState* state, const ChainProgramPlan& plan, const ChainArg& arg,
                    uint32_t coverageId) {
  switch (arg.kind) {
    case ChainArgKind::Register: {
      const auto found = state->registers.find(arg.index);
      return found == state->registers.end() ? 0 : found->second;
    }
    case ChainArgKind::GeomColor:
      return GeomColor(state, plan);
    case ChainArgKind::OpaqueColor:
      return OpaqueColor(state, plan);
    case ChainArgKind::UnitCoverage:
      return coverageId;
    case ChainArgKind::White:
      return state->whiteId;
    case ChainArgKind::Black:
      return state->blackId;
    case ChainArgKind::FragCoord:
      return state->fragCoordId;
    case ChainArgKind::Uniform:
      return LoadMember(state, plan, arg.index);
    case ChainArgKind::Literal:
      return IntConstant(state, arg.index);
    case ChainArgKind::LeafTex:
      break;
  }
  return 0;
}

}  // namespace

bool ChainSPIRVBodySupported(const ChainComposition& composition, const Layout& layout, int hasXp) {
  if (hasXp != 0) {
    return false;
  }
  for (const auto& texture : layout.textures) {
    if (texture.name == "DstTextureSampler") {
      return false;
    }
  }
  for (const auto& slot : composition.slots) {
    switch (slot.op) {
      case ChainISA::None:
      case ChainISA::ConstColor:
      case ChainISA::ColorMatrix:
      case ChainISA::Luma:
      case ChainISA::AlphaThreshold:
      case ChainISA::Blend:
      case ChainISA::InputOpaque:
      case ChainISA::MulAlpha:
      case ChainISA::AARectCoverage:
      case ChainISA::LocalRectCoverage:
      case ChainISA::RRectCoverage:
        break;
      default:
        return false;
    }
  }
  return true;
}

bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>& modules, std::vector<uint32_t>* out,
                    std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  if (modules.size() < 2) {
    *error = "the link needs a frame, a body and at least one op";
    return false;
  }
  spvtools::Context context(SPV_ENV_VULKAN_1_0);
  std::string diagnostic = {};
  context.SetMessageConsumer(
      [&diagnostic](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        if (message != nullptr) {
          diagnostic = message;
        }
      });
  spvtools::LinkerOptions options = {};
  options.SetVerifyIds(true);
  out->clear();
  if (spvtools::Link(context, modules, out, options) != SPV_SUCCESS) {
    *error = diagnostic.empty() ? "the modules did not link" : diagnostic;
    return false;
  }
  return true;
}

bool EmitChainBodySPIRV(const ChainProgramPlan& plan, const Layout& layout,
                        std::vector<uint32_t>* out, std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  BodyState state = {};
  auto& writer = state.writer;
  auto& types = state.types;
  writer.Inst(kOpCapability, {kCapabilityShader});
  writer.Inst(kOpCapability, {kCapabilityLinkage});
  writer.Inst(kOpMemoryModel, {0, 1});  // Logical, GLSL450
  writer.Sink(&writer.decl);

  // The canonical types.
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
  // The sampled image the mask tail reads (declared always: an unused type is legal, the
  // parameter itself only exists on mask variants).
  types.image = writer.NewId();
  writer.Inst(kOpTypeImage, {types.image, types.floatId, 1, 0, 0, 0, 1, 0});
  types.samplerType = writer.NewId();
  writer.Inst(kOpTypeSampler, {types.samplerType});
  types.sampledImage = writer.NewId();
  writer.Inst(kOpTypeSampledImage, {types.sampledImage, types.image});

  // The chainBody parameter list, derived from the variant's own varyings (fragCoord first).
  state.fragCoordId = writer.NewId();
  std::vector<uint32_t> parameterTypes = {};
  std::vector<uint32_t> parameterPointees = {types.vec2};
  std::vector<uint32_t> parameterIds = {state.fragCoordId};
  // The device mask's sampler travels as a parameter when the variant carries one; the runtime
  // gate stays inside the body (HasMaskTexture) so one program serves masked and phantom draws.
  bool hasMaskSampler = false;
  for (const auto& texture : layout.textures) {
    if (texture.name == "MaskTextureSampler") {
      hasMaskSampler = true;
      break;
    }
  }
  uint32_t maskSamplerPointer = 0;
  if (hasMaskSampler) {
    maskSamplerPointer = writer.NewId();
    parameterPointees.push_back(types.sampledImage);
    parameterIds.push_back(maskSamplerPointer);
  }
  for (const auto& varying : layout.varyings) {
    bool ok = true;
    const uint32_t typeId = VaryingTypeId(types, varying.type, &ok);
    if (!ok) {
      *error = "the variant declares a varying the body cannot carry";
      return false;
    }
    const uint32_t id = writer.NewId();
    state.varyingIds[varying.name] = id;
    parameterPointees.push_back(typeId);
    parameterIds.push_back(id);
  }
  for (size_t index = 0; index < parameterPointees.size(); ++index) {
    const bool samplerParameter = hasMaskSampler && index == 1;
    parameterTypes.push_back(samplerParameter
                                 ? UniformConstantPtrTo(&state, parameterPointees[index])
                                 : PtrTo(&state, parameterPointees[index]));
  }

  // The struct: the plan's own members, at the plan's own offsets.
  state.structId = writer.NewId();
  std::vector<uint32_t> memberTypes = {};
  for (const auto& member : plan.uniforms) {
    bool ok = true;
    uint32_t typeId = MemberTypeId(types, member.type, &ok);
    if (!ok || member.count > 1) {
      *error = "the plan declares a member the body cannot carry";
      return false;
    }
    memberTypes.push_back(typeId);
  }
  writer.Inst(kOpTypeStruct, [&] {
    std::vector<uint32_t> operands = {state.structId};
    operands.insert(operands.end(), memberTypes.begin(), memberTypes.end());
    return operands;
  }());
  writer.Sink(&writer.ann);
  writer.Inst(kOpDecorate, {state.structId, kDecorationBlock});
  for (size_t index = 0; index < plan.uniforms.size(); ++index) {
    writer.Inst(kOpMemberDecorate, {state.structId, static_cast<uint32_t>(index), kDecorationOffset,
                                    plan.uniforms[index].offset});
  }
  writer.Sink(&writer.decl);
  const uint32_t uboPointer = writer.NewId();
  writer.Inst(kOpTypePointer, {uboPointer, kStorageUniform, state.structId});
  state.uboId = writer.NewId();
  writer.Inst(kOpVariable, {uboPointer, state.uboId, kStorageUniform});
  // The set and binding decorate the variable, not the block's type.
  writer.Sink(&writer.ann);
  writer.Inst(kOpDecorate, {state.uboId, kDecorationDescriptorSet, FRAGMENT_UBO_DESCRIPTOR_SET});
  writer.Inst(kOpDecorate, {state.uboId, kDecorationBinding, FRAGMENT_UBO_BINDING_POINT});
  writer.Sink(&writer.decl);

  // The template constants the epilogue and the designators use.
  state.whiteId = writer.NewId();
  const uint32_t oneFloat = FloatConstant(&state, 1.0f);
  state.oneFloatId = oneFloat;
  writer.Inst(kOpConstantComposite,
              {types.vec4, state.whiteId, oneFloat, oneFloat, oneFloat, oneFloat});
  const uint32_t zeroFloat = FloatConstant(&state, 0.0f);
  state.blackId = writer.NewId();
  writer.Inst(kOpConstantComposite,
              {types.vec4, state.blackId, zeroFloat, zeroFloat, zeroFloat, zeroFloat});

  // The imported operators the composition reaches.
  std::unordered_map<int, uint32_t> opTypeIds = {};
  for (const auto& call : plan.calls) {
    const OpSignature* signature = FindSignature(call.op);
    if (signature == nullptr) {
      continue;  // the inline expressions need no import
    }
    if (opTypeIds.count(call.op) != 0) {
      continue;
    }
    std::vector<uint32_t> operands = {types.vec4};
    for (ParamKind kind : signature->params) {
      operands.push_back(PtrTo(&state, ParamTypeId(types, kind)));
    }
    const uint32_t functionType = writer.NewId();
    writer.Inst(kOpTypeFunction, [&] {
      std::vector<uint32_t> functionOperands = {functionType};
      functionOperands.insert(functionOperands.end(), operands.begin(), operands.end());
      return functionOperands;
    }());
    const uint32_t functionId = writer.NewId();
    opTypeIds[call.op] = functionType;
    state.opFunctions[call.op] = functionId;
    writer.Sink(&writer.func);
    writer.Inst(kOpFunction, {types.vec4, functionId, 0, functionType});
    for (ParamKind kind : signature->params) {
      const uint32_t parameterId = writer.NewId();
      writer.Inst(kOpFunctionParameter, {PtrTo(&state, ParamTypeId(types, kind)), parameterId});
    }
    writer.Inst(kOpFunctionEnd);
    writer.Sink(&writer.ann);
    // The import decoration (forward reference: the function id already exists).
    const std::string name = signature->name;
    std::string padded = name;
    padded.push_back('\0');
    while (padded.size() % 4 != 0) {
      padded.push_back('\0');
    }
    std::vector<uint32_t> nameWords(padded.size() / 4, 0);
    std::memcpy(nameWords.data(), padded.data(), padded.size());
    std::vector<uint32_t> decoration = {functionId, kDecorationLinkageAttributes};
    decoration.insert(decoration.end(), nameWords.begin(), nameWords.end());
    decoration.push_back(kLinkageTypeImport);
    writer.Inst(kOpDecorate, decoration);
    writer.Sink(&writer.decl);
  }
  if (plan.calls.empty()) {
    *error = "the plan has no calls to link";
    return false;
  }

  // chainBody itself: the exported function the frame calls.
  writer.Sink(&writer.decl);
  const uint32_t chainBodyType = writer.NewId();
  {
    std::vector<uint32_t> functionOperands = {chainBodyType, types.vec4};
    functionOperands.insert(functionOperands.end(), parameterTypes.begin(), parameterTypes.end());
    writer.Inst(kOpTypeFunction, functionOperands);
  }
  const uint32_t chainBodyId = writer.NewId();
  writer.Sink(&writer.ann);
  {
    std::string padded = "chainBody";
    padded.push_back('\0');
    while (padded.size() % 4 != 0) {
      padded.push_back('\0');
    }
    std::vector<uint32_t> nameWords(padded.size() / 4, 0);
    std::memcpy(nameWords.data(), padded.data(), padded.size());
    std::vector<uint32_t> decoration = {chainBodyId, kDecorationLinkageAttributes};
    decoration.insert(decoration.end(), nameWords.begin(), nameWords.end());
    decoration.push_back(kLinkageTypeExport);
    writer.Inst(kOpDecorate, decoration);
  }
  writer.Sink(&writer.func);
  writer.Inst(kOpFunction, {types.vec4, chainBodyId, 0, chainBodyType});
  for (size_t index = 0; index < parameterIds.size(); ++index) {
    writer.Inst(kOpFunctionParameter, {parameterTypes[index], parameterIds[index]});
  }
  const uint32_t labelId = writer.NewId();
  writer.Inst(kOpLabel, {labelId});
  // Every function-scope variable comes first in the block (the SPIR-V rule the drivers
  // enforce): the argument temporaries exist before anything else is emitted.
  std::vector<std::vector<uint32_t>> argumentVariables(plan.calls.size());
  for (size_t callIndex = 0; callIndex < plan.calls.size(); ++callIndex) {
    const auto& call = plan.calls[callIndex];
    const OpSignature* signature = FindSignature(call.op);
    if (signature == nullptr) {
      continue;
    }
    argumentVariables[callIndex].resize(call.args.size(), 0);
    for (size_t argIndex = 0; argIndex < call.args.size() && argIndex < signature->params.size();
         ++argIndex) {
      const uint32_t pointerType = PtrTo(&state, ParamTypeId(types, signature->params[argIndex]));
      const uint32_t variableId = writer.NewId();
      writer.Inst(kOpVariable, {pointerType, variableId, kStorageFunction});
      argumentVariables[callIndex][argIndex] = variableId;
    }
  }
  // The pointer parameters become values once, after the variables.
  std::vector<uint32_t> parameterValues = {};
  for (size_t index = 0; index < parameterIds.size(); ++index) {
    parameterValues.push_back(LoadFrom(&state, parameterIds[index], parameterPointees[index]));
  }
  state.fragCoordId = parameterValues[0];
  uint32_t maskSamplerId = 0;
  size_t valueCursor = 1;
  if (hasMaskSampler) {
    maskSamplerId = parameterValues[valueCursor++];
  }
  for (const auto& varying : layout.varyings) {
    state.varyingIds[varying.name] = parameterValues[valueCursor++];
  }

  // The unit coverage the -3 designator reads.
  const uint32_t coverageId = Splat4(&state, [&] {
    for (const auto& varying : layout.varyings) {
      if (varying.name == "vCoverage") {
        return state.varyingIds["vCoverage"];
      }
    }
    return FloatConstant(&state, 1.0f);
  }());

  // The call sequence.
  for (size_t callIndex = 0; callIndex < plan.calls.size(); ++callIndex) {
    const auto& call = plan.calls[callIndex];
    const OpSignature* signature = FindSignature(call.op);
    std::vector<uint32_t> arguments = {};
    for (size_t argIndex = 0; argIndex < call.args.size(); ++argIndex) {
      const uint32_t value = ResolveArg(&state, plan, call.args[argIndex], coverageId);
      if (value == 0) {
        *error = "an argument could not be resolved";
        return false;
      }
      if (signature != nullptr && argIndex < signature->params.size() &&
          argumentVariables[callIndex][argIndex] != 0) {
        writer.Inst(kOpStore, {argumentVariables[callIndex][argIndex], value});
        arguments.push_back(argumentVariables[callIndex][argIndex]);
      } else {
        arguments.push_back(value);
      }
    }
    uint32_t result = 0;
    switch (call.op) {
      case ChainISA::InputOpaque: {
        result = OpaqueColor(&state, plan);
        break;
      }
      case ChainISA::MulAlpha: {
        const uint32_t alpha = writer.NewId();
        writer.Inst(kOpCompositeExtract, {types.floatId, alpha, arguments[1], 3});
        const uint32_t splat = Splat4(&state, alpha);
        result = writer.NewId();
        writer.Inst(kOpFMul, {types.vec4, result, arguments[0], splat});
        break;
      }
      default: {
        const auto found = state.opFunctions.find(call.op);
        if (found == state.opFunctions.end()) {
          *error = "a call has no linked operator";
          return false;
        }
        result = writer.NewId();
        std::vector<uint32_t> callOperands = {types.vec4, result, found->second};
        callOperands.insert(callOperands.end(), arguments.begin(), arguments.end());
        writer.Inst(kOpFunctionCall, callOperands);
        break;
      }
    }
    if (result == 0) {
      *error = "a call produced no value";
      return false;
    }
    state.registers[call.outRegister] = result;
  }

  // The epilogue: coverage, clip, the plain output and the alpha swizzle.
  const auto rootFound = state.registers.find(plan.rootRegister);
  if (rootFound == state.registers.end()) {
    *error = "the plan did not write a root register";
    return false;
  }
  uint32_t finalCoverage = coverageId;
  if (plan.coverageRootRegister >= 0) {
    const auto found = state.registers.find(plan.coverageRootRegister);
    if (found == state.registers.end()) {
      *error = "the plan did not write a coverage register";
      return false;
    }
    finalCoverage = found->second;
  }
  uint32_t clip = state.whiteId;
  if (plan.clipCoverageRegister >= 0) {
    const auto found = state.registers.find(plan.clipCoverageRegister);
    if (found == state.registers.end()) {
      *error = "the plan did not write a clip register";
      return false;
    }
    clip = found->second;
  }
  // The device mask multiplies the coverage, then the clip folds in, then the result scales —
  // the text route's exact grouping, so the bits stay identical.
  uint32_t coverage = finalCoverage;
  if (hasMaskSampler) {
    const int maskIndex = MemberIndexOf(plan, ChainBindingSource::HasMaskTexture);
    const int matrixIndex = MemberIndexOf(plan, ChainBindingSource::DeviceCoordMatrix);
    if (matrixIndex < 0) {
      *error = "the mask tail has no coordinate matrix member";
      return false;
    }
    const uint32_t matrix = LoadMember(&state, plan, matrixIndex);
    if (matrix == 0) {
      *error = "the mask matrix member could not be read";
      return false;
    }
    const uint32_t one = FloatConstant(&state, 1.0f);
    const uint32_t fragY = writer.NewId();
    writer.Inst(kOpCompositeExtract, {types.floatId, fragY, state.fragCoordId, 1});
    const uint32_t coord3 = writer.NewId();
    writer.Inst(kOpCompositeConstruct, {types.vec3, coord3, state.fragCoordId, fragY, one});
    const uint32_t transformed = writer.NewId();
    writer.Inst(kOpMatrixTimesVector, {types.vec3, transformed, matrix, coord3});
    const uint32_t maskCoord = writer.NewId();
    writer.Inst(kOpVectorShuffle, {types.vec2, maskCoord, transformed, transformed, 0, 1});
    const uint32_t sampled = writer.NewId();
    writer.Inst(kOpImageSampleImplicitLod, {types.vec4, sampled, maskSamplerId, maskCoord});
    const uint32_t maskValue = writer.NewId();
    writer.Inst(kOpCompositeExtract, {types.floatId, maskValue, sampled, 0});
    const uint32_t maskSplat = Splat4(&state, maskValue);
    const uint32_t multiplied = writer.NewId();
    writer.Inst(kOpFMul, {types.vec4, multiplied, maskSplat, coverage});
    if (maskIndex >= 0) {
      // The leaf-bearing profiles gate the multiply at run time: HasMaskTexture != 0.
      const uint32_t hasValue = LoadMember(&state, plan, maskIndex);
      if (hasValue == 0) {
        *error = "the HasMaskTexture member could not be read";
        return false;
      }
      const uint32_t condition = writer.NewId();
      writer.Inst(kOpINotEqual, {types.boolId, condition, hasValue, IntConstant(&state, 0)});
      const uint32_t picked = writer.NewId();
      writer.Inst(kOpSelect, {types.vec4, picked, condition, multiplied, coverage});
      coverage = picked;
    } else {
      coverage = multiplied;
    }
  }
  const uint32_t withClip = writer.NewId();
  writer.Inst(kOpFMul, {types.vec4, withClip, coverage, clip});
  const uint32_t clipped = writer.NewId();
  writer.Inst(kOpFMul, {types.vec4, clipped, rootFound->second, withClip});
  // The alpha swizzle: OutputAlphaSwizzle != 0 selects .aaaa.
  const int swizzleIndex = MemberIndexOf(plan, ChainBindingSource::OutputSwizzle);
  uint32_t output = clipped;
  if (swizzleIndex >= 0) {
    const uint32_t swizzleValue = LoadMember(&state, plan, swizzleIndex);
    if (swizzleValue == 0) {
      *error = "the alpha swizzle member could not be read";
      return false;
    }
    const uint32_t condition = writer.NewId();
    writer.Inst(kOpINotEqual, {types.boolId, condition, swizzleValue, IntConstant(&state, 0)});
    const uint32_t aaaa = writer.NewId();
    writer.Inst(kOpVectorShuffle, {types.vec4, aaaa, clipped, clipped, 3, 3, 3, 3});
    output = writer.NewId();
    writer.Inst(kOpSelect, {types.vec4, output, condition, clipped, aaaa});
  }
  writer.Inst(kOpReturnValue, {output});
  writer.Inst(kOpFunctionEnd);

  writer.Finish(out);
  return true;
}

}  // namespace tgfx

#else  // TGFX_USE_VULKAN

namespace tgfx {

bool ChainSPIRVBodySupported(const ChainComposition&, const Layout&, int) {
  return false;
}

bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>&, std::vector<uint32_t>*,
                    std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan linking route is not part of this build";
  }
  return false;
}

bool EmitChainBodySPIRV(const ChainProgramPlan&, const Layout&, std::vector<uint32_t>*,
                        std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan linking route is not part of this build";
  }
  return false;
}

}  // namespace tgfx

#endif  // TGFX_USE_VULKAN
