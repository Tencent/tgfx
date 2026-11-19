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
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainProgramPlan.h"
#include <map>
#include <set>
#include <unordered_map>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/Module.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {

// The pack entry's text copied out; an empty result means the entry is absent. The walk reads only
// availability, the printer reads the text.
std::string ChainPackText(const PrecompiledShaderCache* cache, uint32_t id) {
  const auto* blob = cache->findPackEntry(id);
  if (blob == nullptr) {
    return {};
  }
  return std::string(blob->data.begin(), blob->data.end());
}

namespace {

// The assembled slice's operator vocabulary: the pointwise color operators, Blend, the inline
// expressions, the analytic coverage operators, plain texture leaves and the resource evaluators
// (gradients, noise). Tiled recipes keep the interpreter's path until their entry material is
// packed too.
bool OpAssembles(int op) {
  switch (op) {
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
    case ChainISA::Texture:
    case ChainISA::TexModulate:
    case ChainISA::Gradient:
    case ChainISA::PerlinNoise:
    case ChainISA::ColorSpaceXform:
      return true;
    default:
      return false;
  }
}

// The std140 rules the emitted block follows: the alignment each type demands and the bytes it
// occupies. The declarations print in the order the offsets accumulate, so the driver and the
// upload agree without an explicit-offset extension.
uint32_t Std140Alignment(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
    case FragmentType::Int:
      return 4;
    case FragmentType::Float2:
      return 8;
    default:
      return 16;
  }
}

uint32_t Std140Size(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
    case FragmentType::Int:
      return 4;
    case FragmentType::Float2:
      return 8;
    case FragmentType::Float3:
      return 12;
    case FragmentType::Float4:
    case FragmentType::Int4:
      return 16;
    case FragmentType::Matrix3:
      return 48;
    case FragmentType::Matrix4:
      return 64;
  }
  return 16;
}

// The std140 stride of an array element: the element's footprint rounds up to its own type's
// alignment, which for a matrix is its 16-byte column alignment.
uint32_t Std140Stride(FragmentType type) {
  const uint32_t alignment = Std140Alignment(type);
  return (Std140Size(type) + alignment - 1) / alignment * alignment;
}

// The semantic name a fragment parameter carries, spelled as the emitted member: the operator's
// table name, the slot, then the parameter. The semantic names live in FragmentLibrary, so a
// renamed parameter is one edit in one place.
std::string MemberName(const char* semantic, size_t slot) {
  const std::string name = semantic;
  const auto dot = name.find('.');
  if (dot == std::string::npos) {
    return name + "_" + std::to_string(slot);
  }
  return name.substr(0, dot) + "_" + std::to_string(slot) + "_" + name.substr(dot + 1);
}

// The reader a fragment parameter's semantic name maps onto. An unknown name is a table/code drift,
// never a default: the caller declines rather than uploading a value nobody asked for.
bool SourceOfSemantic(const char* semantic, ChainBindingSource* source) {
  static const std::map<std::string, ChainBindingSource> table = {
      {"ColorMatrix.linear", ChainBindingSource::SlotColorMatrixLinear},
      {"ColorMatrix.translate", ChainBindingSource::SlotColorMatrixTranslate},
      {"Luma.coefficients", ChainBindingSource::SlotLumaCoefficients},
      {"AlphaThreshold.threshold", ChainBindingSource::SlotAlphaThreshold},
      {"ConstColor.color", ChainBindingSource::SlotConstColor},
      {"AARectCoverage.rect", ChainBindingSource::SlotAARect},
      {"LocalRectCoverage.rect", ChainBindingSource::SlotLocalRect},
      {"LocalRectCoverage.deviceToLocal", ChainBindingSource::SlotLocalRectMatrix},
      {"RRectCoverage.rect", ChainBindingSource::SlotRRect},
      {"RRectCoverage.radiiX", ChainBindingSource::SlotRRectRadiiX},
      {"RRectCoverage.radiiY", ChainBindingSource::SlotRRectRadiiY},
      {"RRectCoverage.antiAlias", ChainBindingSource::SlotRRectAntiAlias},
      {"RRectCoverage.deviceToLocal", ChainBindingSource::SlotRRectMatrix},
  };
  const auto found = table.find(semantic);
  if (found == table.end()) {
    return false;
  }
  *source = found->second;
  return true;
}

bool LayoutDeclaresTexture(const Layout& layout, const char* name) {
  for (const auto& texture : layout.textures) {
    if (texture.name == name) {
      return true;
    }
  }
  return false;
}

bool LayoutDeclaresVarying(const Layout& layout, const char* name) {
  for (const auto& varying : layout.varyings) {
    if (varying.name == name) {
      return true;
    }
  }
  return false;
}

/**
 * The plan's declaration handler: every uniform, varying and sampler is registered as the walk
 * that reads it runs, so the declarations are the plan's closure — what the composition does not
 * read does not exist. `uniform` returns the member's index into the plan's table, so a call
 * argument can name the member without re-spelling it.
 */
class PlanRegistry {
 public:
  int uniform(const std::string& name, FragmentType type, ChainBindingSource source, int slot,
              int index = -1, uint16_t count = 1) {
    const auto found = memberIndex.find(name);
    if (found != memberIndex.end()) {
      return static_cast<int>(found->second);
    }
    const uint32_t alignment = count > 1 ? 16 : Std140Alignment(type);
    const uint32_t stride = count > 1 ? Std140Stride(type) : Std140Size(type);
    const uint32_t offset = (cursor + alignment - 1) / alignment * alignment;
    for (uint16_t element = 0; element < count; ++element) {
      ChainBinding binding = {};
      binding.source = source;
      binding.slot = slot;
      binding.index = count > 1 ? static_cast<int>(element) : index;
      binding.offset = offset + stride * element;
      binding.size = Std140Size(type);
      entries.push_back(binding);
    }
    ChainUniformMember member = {};
    member.name = name;
    member.type = type;
    member.count = count;
    member.source = source;
    member.slot = slot;
    member.index = index;
    member.offset = offset;
    member.size = Std140Size(type);
    memberIndex[name] = members.size();
    members.push_back(std::move(member));
    cursor = offset + stride * count;
    return static_cast<int>(members.size() - 1);
  }

  void varying(const std::string& name, FragmentType type) {
    if (!varyingNames.insert(name).second) {
      return;
    }
    ChainVaryingDecl decl = {};
    decl.name = name;
    decl.type = type;
    varyings.push_back(std::move(decl));
  }

  void sampler(const std::string& name, ChainSamplerSource source, int index) {
    if (!samplerNames.insert(name).second) {
      return;
    }
    ChainSamplerBinding binding = {};
    binding.name = name;
    binding.source = source;
    binding.index = index;
    samplers.push_back(std::move(binding));
  }

  uint32_t blockSize() const {
    return (cursor + 15) / 16 * 16;
  }

  const std::vector<ChainUniformMember>& uniformMembers() const {
    return members;
  }

  const std::vector<ChainVaryingDecl>& varyingDecls() const {
    return varyings;
  }

  const std::vector<ChainSamplerBinding>& samplerBindings() const {
    return samplers;
  }

  const std::vector<ChainBinding>& bindings() const {
    return entries;
  }

 private:
  std::vector<ChainUniformMember> members = {};
  std::map<std::string, size_t> memberIndex = {};
  std::vector<ChainVaryingDecl> varyings = {};
  std::set<std::string> varyingNames = {};
  std::vector<ChainSamplerBinding> samplers = {};
  std::set<std::string> samplerNames = {};
  std::vector<ChainBinding> entries = {};
  uint32_t cursor = 0;
};

// The gradient evaluator's named block: the members the packed text reads, registered only when the
// composition reaches the evaluator. The LUT branch indexes the leaf samplers, so it exists only on
// a variant that declares them.
void RegisterGradientMembers(PlanRegistry* registry, bool lutForm) {
  // The members derive from the gradient entry's GLSL annotations: shader_build_tool --catalog writes
  // ChainOpRegistry.inc, and this is the only place it is included. The assembled route needs the
  // embedded bundles anyway, so a build without them registers nothing.
#if defined(TGFX_EMBED_SHADER_BUNDLES)
#define TGFX_REGISTER_evalGradientSlot
#include "ChainOpRegistry.inc"
#undef TGFX_REGISTER_evalGradientSlot
#else
  (void)registry;
  (void)lutForm;
#endif
}

void RegisterNoiseMembers(PlanRegistry* registry) {
  // The members derive from the entry's GLSL annotations (see ChainOpRegistry.inc).
#if defined(TGFX_EMBED_SHADER_BUNDLES)
#define TGFX_REGISTER_evalNoiseSlot
#include "ChainOpRegistry.inc"
#undef TGFX_REGISTER_evalNoiseSlot
#else
  (void)registry;
#endif
}

// The color-space transform's one chain-wide block, shared by every slot (the matcher admits one).
void RegisterColorSpaceMembers(PlanRegistry* registry) {
  // The members derive from the entry's GLSL annotations (see ChainOpRegistry.inc).
#if defined(TGFX_EMBED_SHADER_BUNDLES)
#define TGFX_REGISTER_chainApplyColorSpaceXform
#include "ChainOpRegistry.inc"
#undef TGFX_REGISTER_chainApplyColorSpaceXform
#else
  (void)registry;
#endif
}

// One leaf's declarations, registered as the leaf enters the plan: the sampler, the coordinate
// varying and the four per-leaf records the plain fetch reads.
void RegisterLeafMembers(PlanRegistry* registry, int leaf) {
  const std::string suffix = std::to_string(leaf);
  registry->sampler("TextureSampler_" + suffix, ChainSamplerSource::Leaf, leaf);
  registry->varying("TransformedCoords_" + suffix, FragmentType::Float3);
  registry->uniform(ChainLeafMemberName("Subset", leaf), FragmentType::Float4,
                    ChainBindingSource::LeafSubset, -1, leaf);
  registry->uniform(ChainLeafMemberName("SubsetClampOn", leaf), FragmentType::Int,
                    ChainBindingSource::LeafSubsetClampOn, -1, leaf);
  registry->uniform(ChainLeafMemberName("AlphaStart", leaf), FragmentType::Float2,
                    ChainBindingSource::LeafAlphaStart, -1, leaf);
  registry->uniform(ChainLeafMemberName("HasRgbaaa", leaf), FragmentType::Int,
                    ChainBindingSource::LeafHasRgbaaa, -1, leaf);
}

// One stored vertex stage member, addressed through the variant's derived block — the stored text is
// the declaration, so its layout is the only authority on the offset. Absent members are silent: the
// variant's own gates decided whether the stored text carries them.
void RegisterVertexMember(ChainBindings* bindings, const Layout& layout, const std::string& name,
                          ChainBindingSource source, int index = -1) {
  for (size_t fieldIndex = 0; fieldIndex < layout.fields.size(); ++fieldIndex) {
    if (layout.fields[fieldIndex].block != 0 || layout.fieldNames[fieldIndex] != name) {
      continue;
    }
    ChainBinding binding = {};
    binding.source = source;
    binding.index = index;
    binding.offset = layout.fields[fieldIndex].offset;
    binding.size = layout.fields[fieldIndex].size;
    bindings->vertex.push_back(binding);
    return;
  }
}

}  // namespace

std::string ChainLeafMemberName(const char* base, int leaf) {
  return leaf == 0 ? std::string(base) : std::string(base) + "_" + std::to_string(leaf);
}

bool PlanChainProgram(const ChainComposition& composition, const Family& family,
                      const Variant& variant, const PrecompiledShaderCache* cache,
                      ChainProgramPlan* plan) {
  if (cache == nullptr || plan == nullptr) {
    return false;
  }
  // The rect-geometry slice: the aliased rect layout whose color and coverage arrive as varyings.
  // The declared geometries (their own modules' fragment hooks) come with the geometry prelude
  // table, not this slice.
  if (variant.geometry != 0) {
    return false;
  }
  // Shader-tiled leaves need the tiling module's runtime loops, which the pack does not spell.
  if ((composition.textureFlags & 2) != 0) {
    return false;
  }
  for (const auto& slot : composition.slots) {
    if (!OpAssembles(slot.op)) {
      return false;
    }
  }
  // The xfer fact picks the resolved support entry: 1 reads the dst through the texture, 2 through
  // framebuffer fetch; 0 carries none. The pack entries themselves are the GL printer's concern
  // (it re-fetches them and declines when they are missing), so the walk stays backend-neutral.
  const int hasXp = XferDimValue(family, variant);
  if (hasXp != 0 && hasXp != 1 && hasXp != 2) {
    return false;
  }

  // The variant's declared sampler set is the authority on which profile carries leaves and the
  // mask; the plan declares only the ones the shape reads.
  const Layout layout = DeriveLayout(variant);
  const bool hasLeafSamplers = LayoutDeclaresTexture(layout, "TextureSampler_0");
  const bool hasMaskSampler = LayoutDeclaresTexture(layout, "MaskTextureSampler");
  const bool hasSubset = DimValueOf(family, variant, 1, 0) != 0;
  // A resource profile gives coordinate transform 0 to the gradient/noise source, so its leaf
  // transforms start at 1. The variant's own SourceCoords declaration is the fact: the stored
  // vertex text was compiled with the same rule (TGFX_POINTWISE_SOURCE_COORDS).
  const uint8_t leafTransformShift = LayoutDeclaresVarying(layout, "SourceCoords") ? 1 : 0;

  std::set<int> usedOps = {};
  for (const auto& slot : composition.slots) {
    if (slot.op != ChainISA::None && slot.outRegister >= 0) {
      usedOps.insert(slot.op);
    }
  }
  const bool hasGradient = usedOps.count(ChainISA::Gradient) != 0;
  const bool hasNoise = usedOps.count(ChainISA::PerlinNoise) != 0;
  // Both resource evaluators address the leaf samplers by a uniform leaf index, so their variant
  // declares all four; the gradient's packed LUT form reads them too.
  const bool gradientLutForm = hasGradient && hasLeafSamplers;

  PlanRegistry registry = {};
  std::vector<ChainCall> calls = {};
  std::set<int> leafSlots = {};
  std::set<int> writtenRegisters = {};
  bool geomColorUsed = false;
  bool opaqueColorUsed = false;
  bool unitCoverageUsed = false;
  bool needsSourceCoords = false;

  // One input designator to one plan argument: the fixed sources, the white/black placeholders the
  // interpreter spells the same way, and a register an earlier call wrote.
  auto resolveArg = [&](int designator, ChainArg* arg) {
    arg->index = 0;
    switch (designator) {
      case -1:
        arg->kind = ChainArgKind::GeomColor;
        geomColorUsed = true;
        return true;
      case -3:
        arg->kind = ChainArgKind::UnitCoverage;
        unitCoverageUsed = true;
        return true;
      case -4:
        arg->kind = ChainArgKind::White;
        return true;
      case -5:
        arg->kind = ChainArgKind::OpaqueColor;
        geomColorUsed = true;
        opaqueColorUsed = true;
        return true;
      default:
        break;
    }
    if (designator < 0) {
      arg->kind = ChainArgKind::Black;
      return true;
    }
    if (writtenRegisters.count(designator) == 0) {
      return false;
    }
    arg->kind = ChainArgKind::Register;
    arg->index = designator;
    return true;
  };
  auto literalArg = [&](int value, ChainArg* arg) {
    arg->kind = ChainArgKind::Literal;
    arg->index = value;
  };
  auto uniformArg = [&](const FragmentDef& def, uint16_t paramIndex, size_t slotIndex,
                        ChainArg* arg) {
    const auto& spec = def.params[paramIndex];
    ChainBindingSource source = ChainBindingSource::SlotConstColor;
    if (!SourceOfSemantic(spec.semantic, &source)) {
      return false;
    }
    arg->kind = ChainArgKind::Uniform;
    arg->index = registry.uniform(MemberName(spec.semantic, slotIndex), spec.type, source,
                                  static_cast<int>(slotIndex));
    return true;
  };

  for (size_t slotIndex = 0; slotIndex < composition.slots.size(); ++slotIndex) {
    const auto& slot = composition.slots[slotIndex];
    if (slot.op == ChainISA::None || slot.outRegister < 0) {
      continue;
    }
    ChainArg in0 = {};
    ChainArg in1 = {};
    if (!resolveArg(slot.in0, &in0) || !resolveArg(slot.in1, &in1)) {
      return false;
    }
    const auto* def = FindFragment(slot.op);
    if (def == nullptr) {
      return false;
    }
    ChainCall call = {};
    call.op = slot.op;
    call.outRegister = slot.outRegister;
    call.multiplyInputAlpha = slot.op == ChainISA::Blend && slot.multiplyInputAlpha;
    auto push = [&](ChainArg arg) { call.args.push_back(arg); };
    switch (slot.op) {
      case ChainISA::ConstColor: {
        ChainArg param = {};
        if (!uniformArg(*def, 0, slotIndex, &param)) {
          return false;
        }
        push(param);
        push(in0);
        ChainArg mode = {};
        literalArg(slot.inputMode, &mode);
        push(mode);
        break;
      }
      case ChainISA::ColorMatrix: {
        push(in0);
        ChainArg matrix = {};
        if (!uniformArg(*def, 0, slotIndex, &matrix)) {
          return false;
        }
        push(matrix);
        ChainArg vector = {};
        if (!uniformArg(*def, 1, slotIndex, &vector)) {
          return false;
        }
        push(vector);
        break;
      }
      case ChainISA::Luma: {
        push(in0);
        ChainArg coefficients = {};
        if (!uniformArg(*def, 0, slotIndex, &coefficients)) {
          return false;
        }
        push(coefficients);
        break;
      }
      case ChainISA::AlphaThreshold: {
        push(in0);
        ChainArg threshold = {};
        if (!uniformArg(*def, 0, slotIndex, &threshold)) {
          return false;
        }
        push(threshold);
        break;
      }
      case ChainISA::Blend: {
        push(in0);
        push(in1);
        ChainArg mode = {};
        literalArg(slot.blendMode, &mode);
        push(mode);
        if (call.multiplyInputAlpha) {
          geomColorUsed = true;
        }
        break;
      }
      case ChainISA::Texture:
        // The plan owns the prefetch, so the slot only modulates the fetched leaf.
        geomColorUsed = true;
        unitCoverageUsed = true;
        leafSlots.insert(static_cast<int>(slotIndex));
        {
          ChainArg tex = {};
          tex.kind = ChainArgKind::LeafTex;
          tex.index = static_cast<int>(slotIndex);
          push(tex);
          ChainArg geom = {};
          geom.kind = ChainArgKind::GeomColor;
          push(geom);
          ChainArg coverage = {};
          coverage.kind = ChainArgKind::UnitCoverage;
          push(coverage);
          ChainArg flags = {};
          literalArg(slot.selector & 0x1F, &flags);
          push(flags);
        }
        break;
      case ChainISA::TexModulate: {
        const int srcSlot = (slot.selector >> 16) & 0xF;
        leafSlots.insert(srcSlot);
        ChainArg tex = {};
        tex.kind = ChainArgKind::LeafTex;
        tex.index = srcSlot;
        push(tex);
        push(in0);
        ChainArg alphaOnly = {};
        literalArg(slot.selector & 1, &alphaOnly);
        push(alphaOnly);
        break;
      }
      case ChainISA::InputOpaque:
        push(in0);
        break;
      case ChainISA::MulAlpha:
        push(in0);
        push(in1);
        break;
      case ChainISA::AARectCoverage: {
        push(in0);
        ChainArg rect = {};
        if (!uniformArg(*def, 0, slotIndex, &rect)) {
          return false;
        }
        push(rect);
        ChainArg frag = {};
        frag.kind = ChainArgKind::FragCoord;
        push(frag);
        break;
      }
      case ChainISA::LocalRectCoverage: {
        push(in0);
        ChainArg rect = {};
        if (!uniformArg(*def, 0, slotIndex, &rect)) {
          return false;
        }
        push(rect);
        ChainArg matrix = {};
        if (!uniformArg(*def, 1, slotIndex, &matrix)) {
          return false;
        }
        push(matrix);
        ChainArg frag = {};
        frag.kind = ChainArgKind::FragCoord;
        push(frag);
        break;
      }
      case ChainISA::RRectCoverage: {
        push(in0);
        for (uint16_t paramIndex = 0; paramIndex < 5; ++paramIndex) {
          ChainArg param = {};
          if (!uniformArg(*def, paramIndex, slotIndex, &param)) {
            return false;
          }
          push(param);
        }
        ChainArg frag = {};
        frag.kind = ChainArgKind::FragCoord;
        push(frag);
        break;
      }
      case ChainISA::ColorSpaceXform:
        RegisterColorSpaceMembers(&registry);
        push(in0);
        break;
      case ChainISA::Gradient:
        RegisterGradientMembers(&registry, gradientLutForm);
        needsSourceCoords = true;
        push(in0);
        break;
      case ChainISA::PerlinNoise:
        RegisterNoiseMembers(&registry);
        needsSourceCoords = true;
        break;
      default:
        return false;
    }
    calls.push_back(std::move(call));
    writtenRegisters.insert(slot.outRegister);
  }
  if (calls.empty() && !composition.slots.empty()) {
    return false;
  }

  // The leaves the body samples, in slot order, each registering its own declarations.
  for (int leaf : leafSlots) {
    if (leaf < 0 || leaf >= 4) {
      return false;
    }
    RegisterLeafMembers(&registry, leaf);
  }
  if (!leafSlots.empty()) {
    // The plain fetch reads the swizzle table and the device-space switch; the strict form also
    // reads the per-quad subset varying the variant declares.
    registry.uniform("LeafReadSwizzles", FragmentType::Int4, ChainBindingSource::LeafReadSwizzles,
                     -1);
    registry.uniform("DeviceLeafMask", FragmentType::Int, ChainBindingSource::DeviceLeafMask, -1);
    registry.uniform("DeviceLeafCoordMatrix", FragmentType::Matrix3,
                     ChainBindingSource::DeviceLeafCoordMatrix, -1, -1, 4);
    if (hasSubset) {
      registry.varying("vTexSubset", FragmentType::Float4);
    }
  }
  // The resource evaluators index the leaf samplers, so their variants need all four declared.
  if (hasLeafSamplers && (gradientLutForm || hasNoise)) {
    for (int leaf = 0; leaf < 4; ++leaf) {
      registry.sampler("TextureSampler_" + std::to_string(leaf), ChainSamplerSource::Leaf, leaf);
    }
  }
  // The device mask is draw coverage. A leaf-free mask profile carries it unconditionally (the mask
  // is part of the matched profile); a four-leaf profile always declares the mask slot — a phantom
  // when the chain has no mask — so it gates the multiply on HasMaskTexture at run time.
  if (hasMaskSampler) {
    registry.sampler("MaskTextureSampler", ChainSamplerSource::Mask, 0);
    registry.uniform("DeviceCoordMatrix", FragmentType::Matrix3,
                     ChainBindingSource::DeviceCoordMatrix, -1);
    if (hasLeafSamplers) {
      registry.uniform("HasMaskTexture", FragmentType::Int, ChainBindingSource::HasMaskTexture, -1);
    }
  }
  if (hasXp != 0) {
    registry.uniform("XPBlendMode", FragmentType::Int, ChainBindingSource::XpBlendMode, -1);
  }
  if (hasXp == 1) {
    registry.sampler("DstTextureSampler", ChainSamplerSource::DstTexture, 0);
    registry.uniform("DstTextureUpperLeft", FragmentType::Float2,
                     ChainBindingSource::DstTextureUpperLeft, -1);
    registry.uniform("DstTextureCoordScale", FragmentType::Float2,
                     ChainBindingSource::DstTextureCoordScale, -1);
  }
  registry.uniform("OutputAlphaSwizzle", FragmentType::Int, ChainBindingSource::OutputSwizzle, -1);
  if (needsSourceCoords) {
    registry.varying("SourceCoords", FragmentType::Float3);
  }
  // The tail's final-coverage expression reads the unit coverage source whenever no coverage
  // subtree replaced it.
  if (composition.coverageRootRegister < 0) {
    unitCoverageUsed = true;
  }
  const bool hasColorVarying = LayoutDeclaresVarying(layout, "vColor");
  if (hasColorVarying) {
    registry.uniform("ChainColorParams_Color", FragmentType::Float4,
                     ChainBindingSource::GeometryColor, -1);
    registry.uniform("ChainColorParams_HasCommonColor", FragmentType::Int,
                     ChainBindingSource::GeometryHasCommonColor, -1);
    registry.varying("vColor", FragmentType::Float4);
  }
  if (unitCoverageUsed) {
    registry.varying("vCoverage", FragmentType::Float);
  }

  // The stored vertex stage's members: the block the stored text declares, filled from the same
  // geometry and program facts the interpreter's writer reads.
  RegisterVertexMember(&plan->bindings, layout, "RTAdjust", ChainBindingSource::RTAdjust);
  RegisterVertexMember(&plan->bindings, layout, "Matrix", ChainBindingSource::VertexMatrix);
  RegisterVertexMember(&plan->bindings, layout, "InTexSubsetMatrix",
                       ChainBindingSource::VertexTexSubsetMatrix);
  for (int leaf : leafSlots) {
    const int transform = leaf + static_cast<int>(leafTransformShift);
    RegisterVertexMember(&plan->bindings, layout,
                         "CoordTransformMatrix_" + std::to_string(transform),
                         ChainBindingSource::CoordTransform, transform);
  }
  if (needsSourceCoords) {
    RegisterVertexMember(&plan->bindings, layout, "CoordTransformMatrix_0",
                         ChainBindingSource::CoordTransform, 0);
  }

  plan->calls = std::move(calls);
  plan->uniforms = registry.uniformMembers();
  plan->varyings = registry.varyingDecls();
  plan->samplers = registry.samplerBindings();
  plan->geomColorUsed = geomColorUsed;
  plan->opaqueColorUsed = opaqueColorUsed;
  plan->unitCoverageUsed = unitCoverageUsed;
  plan->needsSourceCoords = needsSourceCoords;
  plan->hasColorVarying = hasColorVarying;
  plan->hasSubset = hasSubset;
  plan->hasLeafSamplers = hasLeafSamplers;
  plan->hasMaskSampler = hasMaskSampler;
  plan->gradientLutForm = gradientLutForm;
  plan->hasXp = hasXp;
  plan->rootRegister = composition.rootRegister;
  plan->clipCoverageRegister = composition.clipCoverageRegister;
  plan->coverageRootRegister = composition.coverageRootRegister;
  plan->xpNeedsBlendPass = composition.xpNeedsBlendPass;
  plan->leafSlots = std::move(leafSlots);
  plan->usedOps = std::move(usedOps);
  plan->bindings.fragment = registry.bindings();
  plan->bindings.samplers = registry.samplerBindings();
  plan->bindings.leafVaryings = plan->leafSlots;
  plan->bindings.needsSourceCoords = needsSourceCoords;
  plan->bindings.leafTransformShift = leafTransformShift;
  plan->bindings.fragmentBlockSize = registry.blockSize();
  return true;
}

}  // namespace tgfx
