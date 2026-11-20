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
// The runtime link route serves both link-less backends: Vulkan links the module pairs into a
// binary module, Metal links the same modules and then translates the result to MSL.
#if defined(TGFX_USE_VULKAN) || defined(TGFX_USE_METAL)

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/OpTable.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "spirv-tools/libspirv.hpp"
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
  kOpName = 5,
  kOpMemberName = 6,
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
  kOpSelectionMerge = 247,
  kOpBranch = 249,
  kOpBranchConditional = 250,
  kOpLabel = 248,
  kOpReturnValue = 254,
};
enum : uint32_t {
  kDecorationBlock = 2,
  kDecorationColMajor = 5,
  kDecorationArrayStride = 6,
  kDecorationMatrixStride = 7,
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
  std::vector<uint32_t> pre = {};    // capabilities and the memory model
  std::vector<uint32_t> debug = {};  // names
  std::vector<uint32_t> ann = {};    // decorations
  std::vector<uint32_t> decl = {};   // types, constants, variables
  std::vector<uint32_t> func = {};   // functions
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
    out->reserve(5 + pre.size() + debug.size() + ann.size() + decl.size() + func.size());
    out->push_back(0x07230203u);
    out->push_back(0x00010000u);  // SPIR-V 1.0, the op modules' version
    out->push_back(0);
    out->push_back(nextId);
    out->push_back(0);
    out->insert(out->end(), pre.begin(), pre.end());
    out->insert(out->end(), debug.begin(), debug.end());
    out->insert(out->end(), ann.begin(), ann.end());
    out->insert(out->end(), decl.begin(), decl.end());
    out->insert(out->end(), func.begin(), func.end());
  }
};

// The canonical type ids, created once per module.
struct Types {
  uint32_t voidId = 0;
  uint32_t boolId = 0;
  uint32_t boolVec4 = 0;
  uint32_t intId = 0;
  uint32_t floatId = 0;
  uint32_t vec2 = 0;
  uint32_t vec3 = 0;
  uint32_t vec4 = 0;
  uint32_t ivec4 = 0;
  uint32_t mat3 = 0;
  uint32_t mat4 = 0;
  uint32_t vec4Array8 = 0;
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
enum ParamKind : uint8_t {
  kFloat = 0,
  kInt,
  kVec2,
  kVec3,
  kVec4,
  kMat3,
  kMat4,
  kIVec4,
  kVec4Array8,
  kSampledImage
};

struct OpSignature {
  int op = 0;
  const char* name = nullptr;
  std::vector<ParamKind> params = {};
};

// The leaf fetch has no ChainISA value of its own: the body imports it whenever a texture slot
// needs a prefetched leaf, and the bundle carries it under this pseudo-op id.
constexpr int kLeafFetchOp = 0x3F;

// The porter-duff composite's module id: the xfer tail imports it when the draw reads the
// destination through a texture.
constexpr int kPorterDuffOp = 0x3E;

// The ABI kinds the emitter speaks, mapped from the table's vocabulary.
ParamKind ToParamKind(OpTable::Param param) {
  switch (param) {
    case OpTable::Param::Float:
      return kFloat;
    case OpTable::Param::Int:
      return kInt;
    case OpTable::Param::Vec2:
      return kVec2;
    case OpTable::Param::Vec3:
      return kVec3;
    case OpTable::Param::Vec4:
      return kVec4;
    case OpTable::Param::Mat3:
      return kMat3;
    case OpTable::Param::Mat4:
      return kMat4;
    case OpTable::Param::IVec4:
      return kIVec4;
    case OpTable::Param::Vec4Array8:
      return kVec4Array8;
    case OpTable::Param::SampledImage:
      return kSampledImage;
  }
  return kFloat;
}

// The signatures are the operator table's parameter lists: one declaration serves the emitter,
// the build tool and the reconciliation.
const std::vector<OpSignature>& OpSignatures() {
  static const std::vector<OpSignature> signatures = [] {
    std::vector<OpSignature> derived = {};
    for (size_t rowIndex = 0; rowIndex < OpTable::RowCount(); ++rowIndex) {
      const OpTable::Row& row = OpTable::Rows()[rowIndex];
      OpSignature signature = {};
      signature.op = row.id;
      signature.name = row.exportName;
      for (size_t argument = 0; argument < row.argCount; ++argument) {
        signature.params.push_back(ToParamKind(row.args[argument].type));
      }
      derived.push_back(std::move(signature));
    }
    return derived;
  }();
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
    case kIVec4:
      return types.ivec4;
    case kVec4Array8:
      return types.vec4Array8;
    case kSampledImage:
      return types.sampledImage;
  }
  return types.floatId;
}

// One planned call resolved into the module's ids.
struct BodyState {
  Writer writer = {};
  const Layout* layout = nullptr;
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
  std::unordered_map<uint64_t, uint32_t> arrayTypes = {};
  // Identical function signatures share one OpTypeFunction: non-aggregate types must be unique.
  std::vector<std::pair<std::vector<uint32_t>, uint32_t>> functionTypes = {};
};

// A vec4 select with a broadcast condition: SPIR-V 1.0 requires the condition's component count
// to equal the result's, so the scalar becomes a bool vector instead of riding along as-is.
uint32_t SelectVec4(BodyState* state, uint32_t condition, uint32_t trueValue, uint32_t falseValue) {
  const uint32_t conditionVector = state->writer.NewId();
  state->writer.Inst(kOpCompositeConstruct, {state->types.boolVec4, conditionVector, condition,
                                             condition, condition, condition});
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpSelect, {state->types.vec4, id, conditionVector, trueValue, falseValue});
  return id;
}

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

// One array type per (element, count): the stride follows the element's std140 size (vec4 rows
// occupy 16 bytes, mat3 rows 48).
uint32_t ArrayTypeOf(BodyState* state, uint32_t elementType, uint32_t count, uint32_t stride) {
  const uint64_t key = (static_cast<uint64_t>(elementType) << 32) | count;
  const auto found = state->arrayTypes.find(key);
  if (found != state->arrayTypes.end()) {
    return found->second;
  }
  auto* sink = state->writer.sink;
  state->writer.Sink(&state->writer.decl);
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpTypeArray, {id, elementType, IntConstant(state, static_cast<int>(count))});
  state->writer.Sink(&state->writer.ann);
  state->writer.Inst(kOpDecorate, {id, kDecorationArrayStride, stride});
  state->writer.Sink(sink);
  state->arrayTypes[key] = id;
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
    // An array member: the whole array value is what the evaluator parameters carry.
    const uint32_t stride = member.type == FragmentType::Matrix3
                                ? 48u
                                : (member.type == FragmentType::Matrix4 ? 64u : 16u);
    typeId = ArrayTypeOf(state, typeId, member.count, stride);
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

// One element of an array-typed member (OpAccessChain + OpLoad).
uint32_t ArrayElement(BodyState* state, const ChainProgramPlan& plan, int memberIndex,
                      int element) {
  const auto& member = plan.uniforms[static_cast<size_t>(memberIndex)];
  bool ok = true;
  const uint32_t elementType = MemberTypeId(state->types, member.type, &ok);
  if (!ok) {
    return 0;
  }
  const uint32_t pointerType = state->writer.NewId();
  {
    auto* sink = state->writer.sink;
    state->writer.Sink(&state->writer.decl);
    state->writer.Inst(kOpTypePointer, {pointerType, kStorageUniform, elementType});
    state->writer.Sink(sink);
  }
  const uint32_t chainId = state->writer.NewId();
  state->writer.Inst(kOpAccessChain,
                     {pointerType, chainId, state->uboId, IntConstant(state, memberIndex),
                      IntConstant(state, element)});
  const uint32_t valueId = state->writer.NewId();
  state->writer.Inst(kOpLoad, {elementType, valueId, chainId});
  return valueId;
}

// One component of an integer vector value. OpCompositeExtract's index is a literal operand,
// not an id reference.
uint32_t ComponentOf(BodyState* state, uint32_t value, int component) {
  const uint32_t id = state->writer.NewId();
  state->writer.Inst(kOpCompositeExtract,
                     {state->types.intId, id, value, static_cast<uint32_t>(component)});
  return id;
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
  const uint32_t picked = SelectVec4(state, condition, colorId, vColor->second);
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

bool ChainSPIRVBodySupported(const std::set<int>& usedOps, const Layout& layout, int hasXp) {
  if (hasXp != 0 && hasXp != 1) {
    return false;  // the framebuffer-fetch blend pass needs the frame's subpass input
  }
  if (hasXp == 1) {
    bool hasDstSampler = false;
    for (const auto& texture : layout.textures) {
      if (texture.name == "DstTextureSampler") {
        hasDstSampler = true;
      }
    }
    if (!hasDstSampler) {
      return false;
    }
  }
  for (int op : usedOps) {
    const OpTable::Row* row = OpTable::FindRow(op);
    if (row == nullptr || !row->admitted) {
      return false;
    }
  }
  return true;
}

// The runtime's own gate: every module and the linked result are validated before a pipeline is
// built from them, so a malformed emission falls back to the interpreter instead of reaching the
// driver.
bool ValidateSPIRVModule(const std::vector<uint32_t>& words, std::string* error) {
  spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
  std::string diagnostic = {};
  tools.SetMessageConsumer(
      [&diagnostic](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        if (message != nullptr) {
          diagnostic = message;
        }
      });
  if (!tools.Validate(words)) {
    *error = diagnostic.empty() ? "the module failed validation" : diagnostic;
    return false;
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
  return ValidateSPIRVModule(*out, error);
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
  types.boolVec4 = writer.NewId();
  writer.Inst(kOpTypeVector, {types.boolVec4, types.boolId, 4});
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
  // The function-parameter array carries no ArrayStride: glslang's parameter arrays are plain
  // Function-storage arrays, and a decoration would make the linked signatures differ.
  // The function-parameter array type carries no ArrayStride (glslang does not decorate
  // function-scope arrays), so it is its own declaration, distinct from the UBO's array type.
  types.vec4Array8 = writer.NewId();
  writer.Inst(kOpTypeArray, {types.vec4Array8, types.vec4, IntConstant(&state, 8)});
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
  uint32_t maskSamplerBinding = 0;
  for (const auto& texture : layout.textures) {
    if (texture.name == "MaskTextureSampler") {
      hasMaskSampler = true;
      maskSamplerBinding = texture.binding;
    }
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
  for (uint32_t pointee : parameterPointees) {
    // Every parameter is a Function pointer: the sampler no longer travels as one (the body
    // declares its own mask variable), so no UniformConstant case remains here.
    parameterTypes.push_back(PtrTo(&state, pointee));
  }

  // The struct: the plan's own members, at the plan's own offsets.
  state.structId = writer.NewId();
  std::vector<uint32_t> memberTypes = {};
  for (const auto& member : plan.uniforms) {
    bool ok = true;
    uint32_t typeId = MemberTypeId(types, member.type, &ok);
    if (!ok) {
      *error = "the plan declares a member the body cannot carry";
      return false;
    }
    if (member.count > 1) {
      const uint32_t stride = member.type == FragmentType::Matrix3
                                  ? 48u
                                  : (member.type == FragmentType::Matrix4 ? 64u : 16u);
      typeId = ArrayTypeOf(&state, typeId, member.count, stride);
    }
    memberTypes.push_back(typeId);
  }
  writer.Inst(kOpTypeStruct, [&] {
    std::vector<uint32_t> operands = {state.structId};
    operands.insert(operands.end(), memberTypes.begin(), memberTypes.end());
    return operands;
  }());
  // The block carries its name: Vulkan binds by number, but Metal's pipeline matches the
  // uniform block by the name the shader declares, so an anonymous struct would read an
  // unbound buffer.
  writer.Sink(&writer.debug);
  writer.Inst(kOpName, [&] {
    std::vector<uint32_t> operands = {state.structId};
    std::string padded = FragmentUniformBlockName;
    padded.push_back('\0');
    while (padded.size() % 4 != 0) {
      padded.push_back('\0');
    }
    std::vector<uint32_t> nameWords(padded.size() / 4, 0);
    std::memcpy(nameWords.data(), padded.data(), padded.size());
    operands.insert(operands.end(), nameWords.begin(), nameWords.end());
    return operands;
  }());
  for (size_t index = 0; index < plan.uniforms.size(); ++index) {
    const std::string& memberName = plan.uniforms[index].name;
    if (memberName.empty()) {
      continue;
    }
    writer.Inst(kOpMemberName, [&] {
      std::vector<uint32_t> operands = {state.structId, static_cast<uint32_t>(index)};
      std::string padded = memberName;
      padded.push_back('\0');
      while (padded.size() % 4 != 0) {
        padded.push_back('\0');
      }
      std::vector<uint32_t> nameWords(padded.size() / 4, 0);
      std::memcpy(nameWords.data(), padded.data(), padded.size());
      operands.insert(operands.end(), nameWords.begin(), nameWords.end());
      return operands;
    }());
  }
  writer.Sink(&writer.ann);
  writer.Inst(kOpDecorate, {state.structId, kDecorationBlock});
  for (size_t index = 0; index < plan.uniforms.size(); ++index) {
    writer.Inst(kOpMemberDecorate, {state.structId, static_cast<uint32_t>(index), kDecorationOffset,
                                    plan.uniforms[index].offset});
    if (plan.uniforms[index].type == FragmentType::Matrix3 ||
        plan.uniforms[index].type == FragmentType::Matrix4) {
      // The writer stores a mat3 as three 16-byte columns (std140), which is exactly the layout
      // the validator wants spelled out for a Block member.
      writer.Inst(kOpMemberDecorate,
                  {state.structId, static_cast<uint32_t>(index), kDecorationMatrixStride, 16});
      writer.Inst(kOpMemberDecorate,
                  {state.structId, static_cast<uint32_t>(index), kDecorationColMajor});
    }
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

  // The four leaf samplers: the body owns every descriptor declaration, and the operators that
  // sample take them as arguments, so the linked module carries one variable per location.
  uint32_t leafSamplerVars[4] = {0, 0, 0, 0};
  {
    uint32_t leafBindings[4] = {8, 9, 10, 11};
    for (const auto& texture : layout.textures) {
      for (int leaf = 0; leaf < 4; ++leaf) {
        if (texture.name == "TextureSampler_" + std::to_string(leaf)) {
          leafBindings[leaf] = texture.binding;
        }
      }
    }
    for (int leaf = 0; leaf < 4; ++leaf) {
      const uint32_t samplerPointer = UniformConstantPtrTo(&state, types.sampledImage);
      const uint32_t variableId = writer.NewId();
      writer.Inst(kOpVariable, {samplerPointer, variableId, kStorageUniformConstant});
      writer.Sink(&writer.ann);
      writer.Inst(kOpDecorate, {variableId, kDecorationDescriptorSet, TEXTURE_DESCRIPTOR_SET});
      writer.Inst(kOpDecorate, {variableId, kDecorationBinding, leafBindings[leaf]});
      writer.Sink(&writer.decl);
      leafSamplerVars[leaf] = variableId;
    }
  }

  // The mask sampler is the body's own variable (the frame declares none): the tail samples it
  // inside the HasMaskTexture branch, so a draw without a mask never touches the descriptor.
  uint32_t maskSamplerVar = 0;
  if (hasMaskSampler) {
    for (const auto& texture : layout.textures) {
      if (texture.name == "MaskTextureSampler") {
        maskSamplerBinding = texture.binding;
      }
    }
    const uint32_t samplerPointer = UniformConstantPtrTo(&state, types.sampledImage);
    maskSamplerVar = writer.NewId();
    writer.Inst(kOpVariable, {samplerPointer, maskSamplerVar, kStorageUniformConstant});
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate, {maskSamplerVar, kDecorationDescriptorSet, TEXTURE_DESCRIPTOR_SET});
    writer.Inst(kOpDecorate, {maskSamplerVar, kDecorationBinding, maskSamplerBinding});
    writer.Sink(&writer.decl);
  }
  // The destination sampler for the porter-duff tail: the body owns the variable (the module
  // takes it as a parameter), at the layout's own binding.
  uint32_t dstSamplerVar = 0;
  if (plan.hasXp == 1) {
    uint32_t dstBinding = 0;
    for (const auto& texture : layout.textures) {
      if (texture.name == "DstTextureSampler") {
        dstBinding = texture.binding;
      }
    }
    const uint32_t samplerPointer = UniformConstantPtrTo(&state, types.sampledImage);
    dstSamplerVar = writer.NewId();
    writer.Inst(kOpVariable, {samplerPointer, dstSamplerVar, kStorageUniformConstant});
    writer.Sink(&writer.ann);
    writer.Inst(kOpDecorate, {dstSamplerVar, kDecorationDescriptorSet, TEXTURE_DESCRIPTOR_SET});
    writer.Inst(kOpDecorate, {dstSamplerVar, kDecorationBinding, dstBinding});
    writer.Sink(&writer.decl);
  }
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
  std::vector<int> importOps = {};
  bool needsLeafFetch = false;
  for (const auto& call : plan.calls) {
    if (FindSignature(call.op) != nullptr && opTypeIds.count(call.op) == 0) {
      opTypeIds[call.op] = 0;
      importOps.push_back(call.op);
    }
    if (call.op == ChainISA::Texture || call.op == ChainISA::TexModulate) {
      needsLeafFetch = true;
    }
  }
  if (needsLeafFetch) {
    importOps.push_back(kLeafFetchOp);
  }
  if (plan.hasXp == 1) {
    importOps.push_back(kPorterDuffOp);
  }
  for (int importOp : importOps) {
    const OpSignature* signature = FindSignature(importOp);
    if (signature == nullptr) {
      continue;
    }
    auto paramPointer = [&](ParamKind kind) {
      // Opaque parameters live behind a UniformConstant pointer (glslang's convention).
      return kind == kSampledImage ? UniformConstantPtrTo(&state, ParamTypeId(types, kind))
                                   : PtrTo(&state, ParamTypeId(types, kind));
    };
    std::vector<uint32_t> operands = {types.vec4};
    for (ParamKind kind : signature->params) {
      operands.push_back(paramPointer(kind));
    }
    uint32_t functionType = 0;
    for (const auto& cached : state.functionTypes) {
      if (cached.first == operands) {
        functionType = cached.second;
        break;
      }
    }
    if (functionType == 0) {
      functionType = writer.NewId();
      writer.Inst(kOpTypeFunction, [&] {
        std::vector<uint32_t> functionOperands = {functionType};
        functionOperands.insert(functionOperands.end(), operands.begin(), operands.end());
        return functionOperands;
      }());
      state.functionTypes.emplace_back(operands, functionType);
    }
    const uint32_t functionId = writer.NewId();
    state.opFunctions[importOp] = functionId;
    writer.Sink(&writer.func);
    writer.Inst(kOpFunction, {types.vec4, functionId, 0, functionType});
    for (ParamKind kind : signature->params) {
      const uint32_t parameterId = writer.NewId();
      writer.Inst(kOpFunctionParameter, {paramPointer(kind), parameterId});
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
    // The recipes may supply more parameters than the plan's argument list carries (the
    // evaluators' records), so the temporaries follow the signature, not the plan.
    argumentVariables[callIndex].resize(signature->params.size(), 0);
    for (size_t argIndex = 0; argIndex < signature->params.size(); ++argIndex) {
      if (signature->params[argIndex] == kSampledImage) {
        continue;  // the sampler travels as its variable, not a Function temporary
      }
      const uint32_t pointerType = PtrTo(&state, ParamTypeId(types, signature->params[argIndex]));
      const uint32_t variableId = writer.NewId();
      writer.Inst(kOpVariable, {pointerType, variableId, kStorageFunction});
      argumentVariables[callIndex][argIndex] = variableId;
    }
  }
  // The leaf fetch's own argument temporaries, one set per leaf the plan samples.
  std::unordered_map<int, std::vector<uint32_t>> leafArgumentVars = {};
  std::unordered_map<int, uint32_t> leafValues = {};
  const OpSignature* leafSignature = FindSignature(kLeafFetchOp);
  if (needsLeafFetch && leafSignature != nullptr) {
    for (const auto& call : plan.calls) {
      for (const auto& arg : call.args) {
        if (arg.kind != ChainArgKind::LeafTex || leafArgumentVars.count(arg.index) != 0) {
          continue;
        }
        std::vector<uint32_t> vars(leafSignature->params.size(), 0);
        for (size_t paramIndex = 0; paramIndex < leafSignature->params.size(); ++paramIndex) {
          const uint32_t pointerType =
              PtrTo(&state, ParamTypeId(types, leafSignature->params[paramIndex]));
          const uint32_t variableId = writer.NewId();
          writer.Inst(kOpVariable, {pointerType, variableId, kStorageFunction});
          vars[paramIndex] = variableId;
        }
        leafArgumentVars[arg.index] = std::move(vars);
      }
    }
  }
  // The mask tail's coverage temporary: the gated multiply writes through it, so the sample can
  // sit inside a branch (an unbound mask descriptor must never be sampled).
  uint32_t maskCoverageVar = 0;
  if (hasMaskSampler) {
    const uint32_t pointerType = PtrTo(&state, types.vec4);
    maskCoverageVar = writer.NewId();
    writer.Inst(kOpVariable, {pointerType, maskCoverageVar, kStorageFunction});
  }
  // The porter-duff call's value temporaries (the sampler slot stays empty: it travels as the
  // variable itself).
  std::vector<uint32_t> xpArgumentVars = {};
  if (plan.hasXp == 1) {
    const OpSignature* xpSignature = FindSignature(kPorterDuffOp);
    for (size_t paramIndex = 0; xpSignature != nullptr && paramIndex < xpSignature->params.size();
         ++paramIndex) {
      if (xpSignature->params[paramIndex] == kSampledImage) {
        xpArgumentVars.push_back(0);
        continue;
      }
      const uint32_t pointerType =
          PtrTo(&state, ParamTypeId(types, xpSignature->params[paramIndex]));
      const uint32_t variableId = writer.NewId();
      writer.Inst(kOpVariable, {pointerType, variableId, kStorageFunction});
      xpArgumentVars.push_back(variableId);
    }
  }
  // The pointer parameters become values once, after the variables.
  std::vector<uint32_t> parameterValues = {};
  for (size_t index = 0; index < parameterIds.size(); ++index) {
    parameterValues.push_back(LoadFrom(&state, parameterIds[index], parameterPointees[index]));
  }
  state.fragCoordId = parameterValues[0];
  size_t valueCursor = 1;
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

  // The entry-coupled evaluators read their records from the plan's named block; these helpers
  // spell each record as the value its pure parameter expects.
  auto memberValue = [&](ChainBindingSource source) -> uint32_t {
    const int index = MemberIndexOf(plan, source);
    if (index < 0) {
      return 0;
    }
    const auto& member = plan.uniforms[static_cast<size_t>(index)];
    if (member.count > 1) {
      // An array member's value for a function parameter: the UBO's array type carries an
      // ArrayStride the function parameter's type does not, so the whole-array load cannot be
      // stored directly. Load each element and construct the composite in the parameter's type.
      std::vector<uint32_t> elements = {};
      for (uint16_t element = 0; element < member.count; ++element) {
        elements.push_back(ArrayElement(&state, plan, index, element));
      }
      for (uint32_t element : elements) {
        if (element == 0) {
          return 0;
        }
      }
      const uint32_t composite = writer.NewId();
      std::vector<uint32_t> operands = {types.vec4Array8, composite};
      operands.insert(operands.end(), elements.begin(), elements.end());
      writer.Inst(kOpCompositeConstruct, operands);
      return composite;
    }
    return LoadMember(&state, plan, index);
  };
  auto varyingValue = [&](const std::string& name) -> uint32_t {
    const auto found = state.varyingIds.find(name);
    return found == state.varyingIds.end() ? 0 : found->second;
  };
  // One leaf's record, found by its index field (the leaf records share their source with the
  // leaf-0 instance and differ by index).
  auto indexedMember = [&](ChainBindingSource source, int index) -> uint32_t {
    for (size_t memberIndex = 0; memberIndex < plan.uniforms.size(); ++memberIndex) {
      if (plan.uniforms[memberIndex].source == source &&
          plan.uniforms[memberIndex].index == index) {
        return LoadMember(&state, plan, static_cast<int>(memberIndex));
      }
    }
    return 0;
  };
  // One leaf's fetch, cached: the pure fetch takes every device-mask fact and the leaf's own
  // records as values (the samplers live in the fetch module under the layout's leaf bindings).
  std::string recipeError = {};
  auto leafFetchValue = [&](int leaf) -> uint32_t {
    const auto cached = leafValues.find(leaf);
    if (cached != leafValues.end()) {
      return cached->second;
    }
    const auto vars = leafArgumentVars.find(leaf);
    const OpSignature* signature = FindSignature(kLeafFetchOp);
    if (vars == leafArgumentVars.end() || signature == nullptr) {
      return 0;
    }
    int matrixMember = -1;
    int swizzleMember = -1;
    for (size_t memberIndex = 0; memberIndex < plan.uniforms.size(); ++memberIndex) {
      if (plan.uniforms[memberIndex].source == ChainBindingSource::DeviceLeafCoordMatrix) {
        matrixMember = static_cast<int>(memberIndex);
      }
      if (plan.uniforms[memberIndex].source == ChainBindingSource::LeafReadSwizzles) {
        swizzleMember = static_cast<int>(memberIndex);
      }
    }
    if (matrixMember < 0 || swizzleMember < 0) {
      return 0;
    }
    const bool applyQuad = plan.hasSubset && leaf == 0;
    std::vector<uint32_t> values = {
        varyingValue("TransformedCoords_" + std::to_string(leaf)),
        indexedMember(ChainBindingSource::LeafSubset, leaf),
        indexedMember(ChainBindingSource::LeafAlphaStart, leaf),
        indexedMember(ChainBindingSource::LeafHasRgbaaa, leaf),
        indexedMember(ChainBindingSource::LeafSubsetClampOn, leaf),
        IntConstant(&state, leaf),
        memberValue(ChainBindingSource::DeviceLeafMask),
        ArrayElement(&state, plan, matrixMember, leaf),
        ComponentOf(&state, LoadMember(&state, plan, swizzleMember), leaf),
        state.fragCoordId,
        IntConstant(&state, applyQuad ? 1 : 0),
        applyQuad ? varyingValue("vTexSubset") : state.blackId,
    };
    for (size_t parameter = 0; parameter < values.size(); ++parameter) {
      if (values[parameter] == 0) {
        recipeError = "leaf fetch parameter " + std::to_string(parameter) + " of leaf " +
                      std::to_string(leaf);
        return 0;
      }
    }
    for (size_t parameter = 0; parameter < values.size() && parameter < vars->second.size();
         ++parameter) {
      writer.Inst(kOpStore, {vars->second[parameter], values[parameter]});
    }
    const uint32_t result = writer.NewId();
    std::vector<uint32_t> operands = {types.vec4, result, state.opFunctions[kLeafFetchOp]};
    operands.insert(operands.end(), vars->second.begin(), vars->second.end());
    writer.Inst(kOpFunctionCall, operands);
    leafValues[leaf] = result;
    return result;
  };
  // The call sequence.
  for (size_t callIndex = 0; callIndex < plan.calls.size(); ++callIndex) {
    const auto& call = plan.calls[callIndex];
    const OpSignature* signature = FindSignature(call.op);
    std::vector<uint32_t> values = {};
    const OpTable::Row* row = OpTable::FindRow(call.op);
    if (row == nullptr) {
      // The inline expressions (InputOpaque, MulAlpha) own no module: their arguments resolve
      // directly and the result switch below spells the expression.
      for (const auto& arg : call.args) {
        values.push_back(arg.kind == ChainArgKind::LeafTex
                             ? leafFetchValue(arg.index)
                             : ResolveArg(&state, plan, arg, coverageId));
      }
    } else if (row->special != nullptr && row->special[0] != '\0') {
      *error = "a special operator cannot ride a plan call";
      return false;
    } else {
      for (size_t argument = 0; argument < row->argCount; ++argument) {
        const OpTable::Arg& spec = row->args[argument];
        uint32_t value = 0;
        switch (spec.source) {
          case OpTable::Source::CallArg: {
            if (spec.a >= 0 && static_cast<size_t>(spec.a) < call.args.size()) {
              const ChainArg& arg = call.args[static_cast<size_t>(spec.a)];
              value = arg.kind == ChainArgKind::LeafTex ? leafFetchValue(arg.index)
                                                        : ResolveArg(&state, plan, arg, coverageId);
            }
            break;
          }
          case OpTable::Source::Member:
            value = memberValue(static_cast<ChainBindingSource>(spec.a));
            break;
          case OpTable::Source::MemberOrZero: {
            const int memberIndex = MemberIndexOf(plan, static_cast<ChainBindingSource>(spec.a));
            value =
                memberIndex < 0 ? IntConstant(&state, 0) : LoadMember(&state, plan, memberIndex);
            break;
          }
          case OpTable::Source::MemberIndexed:
            value = indexedMember(static_cast<ChainBindingSource>(spec.a), spec.b);
            break;
          case OpTable::Source::ArrayElement: {
            int memberIndex = -1;
            for (size_t index = 0; index < plan.uniforms.size(); ++index) {
              if (plan.uniforms[index].source == static_cast<ChainBindingSource>(spec.a)) {
                memberIndex = static_cast<int>(index);
              }
            }
            value = memberIndex < 0 ? 0 : ArrayElement(&state, plan, memberIndex, spec.b);
            break;
          }
          case OpTable::Source::LeafSampler:
            value = leafSamplerVars[spec.a];
            break;
          case OpTable::Source::Varying:
            value = varyingValue(spec.a == 0 ? "SourceCoords" : "vTexSubset");
            break;
          case OpTable::Source::Constant:
            value = IntConstant(&state, spec.a);
            break;
          case OpTable::Source::FragCoord:
            value = state.fragCoordId;
            break;
          case OpTable::Source::Special:
            break;  // the row's dedicated recipe builds it
        }
        values.push_back(value);
      }
    }
    if (std::find(values.begin(), values.end(), 0u) != values.end()) {
      *error = "an argument could not be resolved for op " + std::to_string(call.op) +
               (recipeError.empty() ? std::string() : ": " + recipeError);
      return false;
    }
    std::vector<uint32_t> arguments = {};
    for (size_t argIndex = 0; argIndex < values.size(); ++argIndex) {
      if (signature != nullptr && argIndex < signature->params.size() &&
          argumentVariables[callIndex][argIndex] != 0 &&
          signature->params[argIndex] != kSampledImage) {
        writer.Inst(kOpStore, {argumentVariables[callIndex][argIndex], values[argIndex]});
        arguments.push_back(argumentVariables[callIndex][argIndex]);
      } else {
        arguments.push_back(values[argIndex]);
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
          *error = "a call has no linked operator for op " + std::to_string(call.op);
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
    // The sample sits inside a branch: a draw without a mask leaves the descriptor unbound, and
    // sampling it would fault. The leaf-bearing profiles gate on HasMaskTexture at run time; the
    // leaf-free mask profiles carry the multiply unconditionally.
    uint32_t thenLabel = 0;
    uint32_t mergeLabel = 0;
    if (maskIndex >= 0) {
      const uint32_t hasValue = LoadMember(&state, plan, maskIndex);
      if (hasValue == 0) {
        *error = "the HasMaskTexture member could not be read";
        return false;
      }
      const uint32_t condition = writer.NewId();
      writer.Inst(kOpINotEqual, {types.boolId, condition, hasValue, IntConstant(&state, 0)});
      writer.Inst(kOpStore, {maskCoverageVar, coverage});
      thenLabel = writer.NewId();
      mergeLabel = writer.NewId();
      writer.Inst(kOpSelectionMerge, {mergeLabel, 0});
      writer.Inst(kOpBranchConditional, {condition, thenLabel, mergeLabel});
      writer.Inst(kOpLabel, {thenLabel});
    }
    const uint32_t matrix = LoadMember(&state, plan, matrixIndex);
    if (matrix == 0) {
      *error = "the mask matrix member could not be read";
      return false;
    }
    const uint32_t one = FloatConstant(&state, 1.0f);
    // vec3(fragCoord.xy, 1.0): the vec2 plus the one, no extra component.
    const uint32_t coord3 = writer.NewId();
    writer.Inst(kOpCompositeConstruct, {types.vec3, coord3, state.fragCoordId, one});
    const uint32_t transformed = writer.NewId();
    writer.Inst(kOpMatrixTimesVector, {types.vec3, transformed, matrix, coord3});
    const uint32_t maskCoord = writer.NewId();
    writer.Inst(kOpVectorShuffle, {types.vec2, maskCoord, transformed, transformed, 0, 1});
    const uint32_t maskSampler = LoadFrom(&state, maskSamplerVar, types.sampledImage);
    const uint32_t sampled = writer.NewId();
    writer.Inst(kOpImageSampleImplicitLod, {types.vec4, sampled, maskSampler, maskCoord});
    const uint32_t maskValue = writer.NewId();
    writer.Inst(kOpCompositeExtract, {types.floatId, maskValue, sampled, 0});
    const uint32_t maskSplat = Splat4(&state, maskValue);
    const uint32_t multiplied = writer.NewId();
    writer.Inst(kOpFMul, {types.vec4, multiplied, maskSplat, coverage});
    if (maskIndex >= 0) {
      writer.Inst(kOpStore, {maskCoverageVar, multiplied});
      writer.Inst(kOpBranch, {mergeLabel});
      writer.Inst(kOpLabel, {mergeLabel});
      coverage = LoadFrom(&state, maskCoverageVar, types.vec4);
    } else {
      coverage = multiplied;
    }
  }
  const uint32_t withClip = writer.NewId();
  writer.Inst(kOpFMul, {types.vec4, withClip, coverage, clip});
  const uint32_t clipped = writer.NewId();
  writer.Inst(kOpFMul, {types.vec4, clipped, rootFound->second, withClip});
  // The xfer tail: the porter-duff composite over the destination texture, before the alpha
  // swizzle (the text route's order).
  uint32_t outputSource = clipped;
  if (plan.hasXp == 1) {
    const int upperLeftIndex = MemberIndexOf(plan, ChainBindingSource::DstTextureUpperLeft);
    const int scaleIndex = MemberIndexOf(plan, ChainBindingSource::DstTextureCoordScale);
    const int modeIndex = MemberIndexOf(plan, ChainBindingSource::XpBlendMode);
    if (upperLeftIndex < 0 || scaleIndex < 0 || modeIndex < 0 || dstSamplerVar == 0 ||
        xpArgumentVars.size() < 6) {
      *error = "the xfer tail has no destination records";
      return false;
    }
    const uint32_t values[6] = {rootFound->second,
                                withClip,
                                state.fragCoordId,
                                LoadMember(&state, plan, upperLeftIndex),
                                LoadMember(&state, plan, scaleIndex),
                                LoadMember(&state, plan, modeIndex)};
    for (size_t index = 0; index < 6; ++index) {
      if (values[index] == 0 || xpArgumentVars[index] == 0) {
        *error = "an xfer tail argument could not be read";
        return false;
      }
      writer.Inst(kOpStore, {xpArgumentVars[index], values[index]});
    }
    const uint32_t xpResult = writer.NewId();
    std::vector<uint32_t> operands = {types.vec4, xpResult, state.opFunctions[kPorterDuffOp]};
    for (uint32_t argument : xpArgumentVars) {
      if (argument != 0) {
        operands.push_back(argument);  // the sampler's slot carries the variable below
      }
    }
    operands.push_back(dstSamplerVar);
    writer.Inst(kOpFunctionCall, operands);
    outputSource = xpResult;
  }
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
    writer.Inst(kOpVectorShuffle, {types.vec4, aaaa, outputSource, outputSource, 3, 3, 3, 3});
    // The text route's semantic: a non-zero swizzle writes `fragColor.aaaa` (an alpha-only
    // render target), everything else keeps the colour.
    output = SelectVec4(&state, condition, aaaa, outputSource);
  }
  writer.Inst(kOpReturnValue, {output});
  writer.Inst(kOpFunctionEnd);

  writer.Finish(out);
  return true;
}

}  // namespace tgfx

#else  // TGFX_USE_VULKAN || TGFX_USE_METAL

namespace tgfx {

bool ChainSPIRVBodySupported(const std::set<int>&, const Layout&, int) {
  return false;
}

bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>&, std::vector<uint32_t>*,
                    std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan linking route is not part of this build";
  }
  return false;
}

bool ValidateSPIRVModule(const std::vector<uint32_t>&, std::string* error) {
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

#endif  // TGFX_USE_VULKAN || TGFX_USE_METAL
