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
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainProgramPlan.h"
#include <map>
#include <set>
#include <string>
#include "core/utils/Log.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/ir/ChainLayoutMath.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/KernelContract.h"
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
// (gradients, noise). Tiled recipes keep the runtime path until their entry material is packed
// too.
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
    case ChainISA::YUV:
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
 * that reads it runs, so the declarations are the plan's closure — what the expression does not
 * read does not exist. `uniform` returns the member's index into the plan's table, so a call
 * argument can name the member without re-spelling it. The current node names the graph site the
 * next registrations belong to: the walk sets it before it registers an operator's members, so
 * every member and binding carries the node its parameter reader addresses.
 */
class PlanRegistry {
 public:
  void setNode(const AOTEffectNode* node) {
    currentNode = node;
  }

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
      binding.node = currentNode;
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
    member.node = currentNode;
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
  const AOTEffectNode* currentNode = nullptr;
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
// expression reaches the evaluator. The LUT branch indexes the leaf samplers, so it exists only on
// a variant that declares them.
void RegisterGradientMembers(PlanRegistry* registry, bool lutForm) {
  // The members derive from the gradient entry's GLSL annotations: shader_build_tool --catalog writes
  // ChainOpRegistry.inc, and this is the only place it is included. The assembled route needs the
  // embedded bundles anyway, so a build without them registers nothing.
#if defined(TGFX_EMBED_SHADER_BUNDLES)
#define TGFX_REGISTER_evalGradientSlot
#include "ChainOpRegistry.inc"
#undef TGFX_REGISTER_evalGradientSlot
  (void)lutForm;
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

// The color-space transform's one chain-wide block, shared by every operator (the matcher admits
// one).
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

// The YUV source's members: the names match the generated registration (the signature's derived
// spelling), but each carries the operator's own slot, so the reader finds the site's record. The
// three planes ride the leaf-sampler bindings at the site's own index and the two above it; the
// coordinate is the assembly's own SourceCoords (the same varying the noise reads).
void RegisterYUVMembers(PlanRegistry* registry, int slot) {
  const int plane[3] = {slot, slot + 1, slot + 2};
  for (int index : plane) {
    registry->sampler("TextureSampler_" + std::to_string(index), ChainSamplerSource::Leaf, index);
  }
  registry->uniform("YuvSubset", FragmentType::Float4, ChainBindingSource::YuvSubset, slot);
  registry->uniform("YuvAlphaStart", FragmentType::Float2, ChainBindingSource::YuvAlphaStart, slot);
  registry->uniform("YuvColorConversion", FragmentType::Matrix3,
                    ChainBindingSource::YuvColorConversion, slot);
  registry->uniform("YuvLimitedRange", FragmentType::Int, ChainBindingSource::YuvLimitedRange,
                    slot);
  registry->uniform("YuvHasRgbaaa", FragmentType::Int, ChainBindingSource::YuvHasRgbaaa, slot);
  registry->uniform("YuvFormat", FragmentType::Int, ChainBindingSource::YuvFormat, slot);
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
// The stored vertex block names a member bare for the chain's own rect modules and with the
// geometry module's prefix for the reused per-pixel geometries (ElementNameOf's rule): a geometry
// module's own uniform is <Module>_<name> (HairlineLineGeometry_Matrix), while the chain contract
// members (RTAdjust, CoordTransformMatrix_n) stay bare. The prefix candidate lets one call site
// serve both spellings; a name absent from both is a contract mismatch the caller would see as an
// unwritten member, so the lookup must cover every geometry's spelling.
// The stored vertex block names every member with the module prefix that declares it (the chain
// contract members RTAdjust and CoordTransformMatrix_n carry the bare spelling the chain module's
// own text uses). The caller passes the empty prefix for the contract members and the geometry
// module's prefix for its own uniforms, so each lookup has exactly one spelling to match.
void RegisterVertexMember(ChainBindings* bindings, const Layout& layout, const std::string& name,
                          const std::string& prefix, ChainBindingSource source, int index = -1) {
  for (size_t fieldIndex = 0; fieldIndex < layout.fields.size(); ++fieldIndex) {
    if (layout.fields[fieldIndex].block != 0 || layout.fieldNames[fieldIndex] != prefix + name) {
      continue;
    }
    ChainBinding binding = {};
    binding.source = source;
    binding.index = index;
    binding.offset = layout.fields[fieldIndex].offset;
    binding.size = layout.fields[fieldIndex].size;
    bindings->vertex.push_back(binding);
  }
}

/// The shared walk's frame: the sites in instruction order (dead instructions keep their place —
/// the numbering is the plan's member-naming input), the epilogue registers the tail reads, and the
/// xfer's blend-pass fact.
struct ChainWalkFrame {
  std::vector<AOTChainCallSite> sites = {};
  /// The leaves whose recipe names a shader-side tiled sample (their fetch maps through the
  /// tiled catalog functions with the recipe's own members).
  std::set<int> tiledLeafSlots = {};
  int rootRegister = 0;
  int coverageRootRegister = -1;
  int clipCoverageRegister = -1;
  bool xpNeedsBlendPass = false;
};

/**
 * The shared plan walk: resolves every argument against the frame's designators and registers,
 * registers every uniform, varying and sampler the body reads (the declaration closure), and
 * collects the interface facts the printer and the linker spell from. The walk is a pure function
 * of the frame and the variant, so the graph layout that builds the frame spells one plan.
 */
bool WalkChainProgram(const ChainWalkFrame& frame, const Family& family, const Variant& variant,
                      ChainProgramPlan* plan) {
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
  for (const auto& site : frame.sites) {
    if (site.op != ChainISA::None && site.outRegister >= 0) {
      usedOps.insert(site.op);
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

  for (size_t slotIndex = 0; slotIndex < frame.sites.size(); ++slotIndex) {
    const auto& site = frame.sites[slotIndex];
    if (site.op == ChainISA::None || site.outRegister < 0) {
      continue;
    }
    ChainArg in0 = {};
    ChainArg in1 = {};
    if (!resolveArg(site.in0, &in0) || !resolveArg(site.in1, &in1)) {
      return false;
    }
    const auto* def = FindFragment(site.op);
    if (def == nullptr) {
      return false;
    }
    // Every member this site registers carries its graph node: the upload-time readers address
    // the operator's parameter block through it.
    registry.setNode(site.node);
    ChainCall call = {};
    call.op = site.op;
    call.outRegister = site.outRegister;
    call.multiplyInputAlpha = site.op == ChainISA::Blend && site.multiplyInputAlpha;
    auto push = [&](ChainArg arg) { call.args.push_back(arg); };
    switch (site.op) {
      case ChainISA::ConstColor: {
        ChainArg param = {};
        if (!uniformArg(*def, 0, slotIndex, &param)) {
          return false;
        }
        push(param);
        push(in0);
        ChainArg mode = {};
        literalArg(site.inputMode, &mode);
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
        literalArg(site.blendMode, &mode);
        push(mode);
        if (call.multiplyInputAlpha) {
          geomColorUsed = true;
        }
        break;
      }
      case ChainISA::Texture:
        // The plan owns the prefetch, so the site only modulates the fetched leaf.
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
          literalArg(site.textureFlags, &flags);
          push(flags);
        }
        break;
      case ChainISA::TexModulate: {
        const int srcSlot = site.texModulateSourceSlot;
        leafSlots.insert(srcSlot);
        ChainArg tex = {};
        tex.kind = ChainArgKind::LeafTex;
        tex.index = srcSlot;
        push(tex);
        push(in0);
        ChainArg alphaOnly = {};
        literalArg(site.texModulateAlphaOnly ? 1 : 0, &alphaOnly);
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
      case ChainISA::YUV: {
        RegisterYUVMembers(&registry, static_cast<int>(slotIndex));
        needsSourceCoords = true;
        geomColorUsed = true;
        ChainArg geom = {};
        geom.kind = ChainArgKind::GeomColor;
        push(geom);
        // The three planes ride the leaf samplers at the call's own slot and the two above it.
        ChainArg planes = {};
        planes.kind = ChainArgKind::LeafTex;
        planes.index = static_cast<int>(slotIndex);
        push(planes);
        break;
      }
      default:
        return false;
    }
    calls.push_back(std::move(call));
    writtenRegisters.insert(site.outRegister);
  }
  if (calls.empty() && !frame.sites.empty()) {
    return false;
  }

  // The geometry module's member prefix: the reused per-pixel geometries and the chain's own rect
  // modules all spell their own members as <Module>_<name> (ElementNameOf's rule), so one prefix
  // serves the geometry-owned varyings and vertex uniforms alike.
  const std::string geometryPrefix =
      variant.geometry < 9 && ChainGeometries[variant.geometry] != nullptr
          ? std::string(ChainGeometries[variant.geometry]->name) + "_"
          : std::string();
  // The leaves the body samples, in slot order, each registering its own declarations. The leaf
  // records read the program's binding table, so they carry no node.
  for (int leaf : leafSlots) {
    if (leaf < 0 || leaf >= 4) {
      return false;
    }
    RegisterLeafMembers(&registry, leaf);
    if (frame.tiledLeafSlots.count(leaf) != 0) {
      // The tiled recipe rides the call arguments: each tiled leaf spells its mode pair, subset,
      // clamp rect and dimension as its own members, so one assembly serves any draw's recipe.
      const std::string suffix = "_" + std::to_string(leaf);
      registry.setNode(nullptr);
      registry.uniform("TiledModeX" + suffix, FragmentType::Int,
                       ChainBindingSource::TiledShaderModeX, -1, leaf);
      registry.uniform("TiledModeY" + suffix, FragmentType::Int,
                       ChainBindingSource::TiledShaderModeY, -1, leaf);
      registry.uniform("TiledSubset" + suffix, FragmentType::Float4,
                       ChainBindingSource::TiledSubset, -1, leaf);
      registry.uniform("TiledClamp" + suffix, FragmentType::Float4, ChainBindingSource::TiledClamp,
                       -1, leaf);
      registry.uniform("TiledDimension" + suffix, FragmentType::Float2,
                       ChainBindingSource::TiledDimension, -1, leaf);
      registry.uniform("TiledStrict" + suffix, FragmentType::Int, ChainBindingSource::TiledStrict,
                       -1, leaf);
    }
  }
  if (!leafSlots.empty()) {
    // The plain fetch reads the swizzle table and the device-space switch; the strict form also
    // reads the per-quad subset varying the variant declares.
    registry.setNode(nullptr);
    registry.uniform("LeafReadSwizzles", FragmentType::Int4, ChainBindingSource::LeafReadSwizzles,
                     -1);
    registry.uniform("DeviceLeafMask", FragmentType::Int, ChainBindingSource::DeviceLeafMask, -1);
    registry.uniform("DeviceLeafCoordMatrix", FragmentType::Matrix3,
                     ChainBindingSource::DeviceLeafCoordMatrix, -1, -1, 4);
    if (hasSubset) {
      registry.varying(geometryPrefix + "vTexSubset", FragmentType::Float4);
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
  if (frame.coverageRootRegister < 0) {
    unitCoverageUsed = true;
  }
  // The per-vertex color varying: the rect layouts declare it bare (vColor), the per-pixel
  // geometries with their module prefix (EllipseGeometry_vColor, ...); one suffix match covers
  // both spellings.
  std::string colorVarying = {};
  for (const auto& varying : layout.varyings) {
    if (IsColorVaryingName(varying.name)) {
      colorVarying = varying.name;
    }
  }
  const bool hasColorVarying = !colorVarying.empty();
  if (hasColorVarying) {
    registry.uniform("ChainColorParams_Color", FragmentType::Float4,
                     ChainBindingSource::GeometryColor, -1);
    registry.uniform("ChainColorParams_HasCommonColor", FragmentType::Int,
                     ChainBindingSource::GeometryHasCommonColor, -1);
    registry.varying(colorVarying, FragmentType::Float4);
  }
  // A per-pixel-coverage geometry declares its own varyings and the ChainColorParams switches
  // its edge formula reads, and the unit coverage source becomes the formula's value (the rect
  // layouts keep the vCoverage varying instead).
  const ChainGeometryCoverageCall* geometryCoverage = ChainGeometryCoverageOf(variant.geometry);
  if (geometryCoverage != nullptr) {
    for (size_t arg = 0; arg < geometryCoverage->argCount; ++arg) {
      const auto& parameter = geometryCoverage->args[arg];
      if (parameter.uniform) {
        registry.uniform(parameter.name, parameter.type, parameter.source, -1);
      } else {
        registry.varying(parameter.name, parameter.type);
      }
    }
  }
  if (unitCoverageUsed && geometryCoverage == nullptr) {
    registry.varying(geometryPrefix + "vCoverage", FragmentType::Float);
  }

  // The stored vertex stage's members: the block the stored text declares, filled from the same
  // geometry and program facts the writer reads. The geometry module's own uniforms carry its
  // prefix; the chain contract members stay bare.
  RegisterVertexMember(&plan->bindings, layout, "RTAdjust", "", ChainBindingSource::RTAdjust);
  RegisterVertexMember(&plan->bindings, layout, "Matrix", geometryPrefix,
                       ChainBindingSource::VertexMatrix);
  RegisterVertexMember(&plan->bindings, layout, "InTexSubsetMatrix", geometryPrefix,
                       ChainBindingSource::VertexTexSubsetMatrix);
  for (int leaf : leafSlots) {
    const int transform = leaf + static_cast<int>(leafTransformShift);
    RegisterVertexMember(&plan->bindings, layout,
                         "CoordTransformMatrix_" + std::to_string(transform), "",
                         ChainBindingSource::CoordTransform, transform);
  }
  if (needsSourceCoords) {
    RegisterVertexMember(&plan->bindings, layout, "CoordTransformMatrix_0", "",
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
  plan->rootRegister = frame.rootRegister;
  plan->clipCoverageRegister = frame.clipCoverageRegister;
  plan->coverageRootRegister = frame.coverageRootRegister;
  plan->xpNeedsBlendPass = frame.xpNeedsBlendPass;
  plan->leafSlots = std::move(leafSlots);
  plan->tiledLeafSlots = frame.tiledLeafSlots;
  plan->usedOps = std::move(usedOps);
  plan->geometry = variant.geometry;
  plan->bindings.fragment = registry.bindings();
  plan->bindings.samplers = registry.samplerBindings();
  plan->bindings.leafVaryings = plan->leafSlots;
  plan->bindings.needsSourceCoords = needsSourceCoords;
  plan->bindings.leafTransformShift = leafTransformShift;
  plan->bindings.fragmentBlockSize = registry.blockSize();
  return true;
}

}  // namespace

// The per-pixel geometry coverage calls, one per non-rect geometry the chain family serves: the
// catalog function (geometry/*_coverage.inc) and its arguments in call order. The varying names
// are the stored variant's own prefixed declarations (see pointwise_fragment_main.inc's alias
// block); the uniforms are the ChainColorParams switches the interpreter text read. One table
// serves the walk (which declares the members) and the printer (which spells the call), so the
// two cannot drift about a geometry's contract.
namespace {

constexpr ChainGeometryCoverageArg EllipseCoverageArgs[] = {
    {"EllipseGeometry_vOffsets", FragmentType::Float2},
    {"EllipseGeometry_vRadii", FragmentType::Float4},
    {"ChainColorParams_StrokeEnabled", FragmentType::Int, true, ChainBindingSource::GeometryStroke},
};
constexpr ChainGeometryCoverageArg HairlineLineCoverageArgs[] = {
    {"HairlineLineGeometry_vEdge", FragmentType::Float},
    {"ChainColorParams_Coverage", FragmentType::Float, true, ChainBindingSource::GeometryCoverage},
    {"ChainColorParams_AAEnabled", FragmentType::Int, true, ChainBindingSource::GeometryAA},
};
constexpr ChainGeometryCoverageArg ComplexEllipseCoverageArgs[] = {
    {"ComplexEllipseGeometry_vOffsets", FragmentType::Float2},
    {"ComplexEllipseGeometry_vRadii", FragmentType::Float4},
    {"ComplexEllipseGeometry_vEdgeDist", FragmentType::Float2},
    {"ComplexEllipseGeometry_vStrokeWidth", FragmentType::Float2},
    {"ChainColorParams_StrokeEnabled", FragmentType::Int, true, ChainBindingSource::GeometryStroke},
};
constexpr ChainGeometryCoverageArg NonAARRectCoverageArgs[] = {
    {"RRectGeometry_vLocalCoord", FragmentType::Float2},
    {"RRectGeometry_vRectBounds", FragmentType::Float4},
    {"RRectGeometry_vRadii", FragmentType::Float2},
    {"RRectGeometry_vStrokeWidth", FragmentType::Float2},
    {"ChainColorParams_StrokeEnabled", FragmentType::Int, true, ChainBindingSource::GeometryStroke},
};
constexpr ChainGeometryCoverageArg ComplexRRectCoverageArgs[] = {
    {"ComplexRRectGeometry_vLocalCoord", FragmentType::Float2},
    {"ComplexRRectGeometry_vRectBounds", FragmentType::Float4},
    {"ComplexRRectGeometry_vXRadii", FragmentType::Float4},
    {"ComplexRRectGeometry_vYRadii", FragmentType::Float4},
    {"ComplexRRectGeometry_vStrokeWidth", FragmentType::Float2},
    {"ChainColorParams_StrokeEnabled", FragmentType::Int, true, ChainBindingSource::GeometryStroke},
};
constexpr ChainGeometryCoverageArg HairlineQuadCoverageArgs[] = {
    {"HairlineQuadGeometry_vHairQuadEdge", FragmentType::Float4},
    {"ChainColorParams_Coverage", FragmentType::Float, true, ChainBindingSource::GeometryCoverage},
    {"ChainColorParams_AAEnabled", FragmentType::Int, true, ChainBindingSource::GeometryAA},
};

constexpr ChainGeometryCoverageCall GeometryCoverageCalls[] = {
    {3, "ellipseEdgeCoverage", EllipseCoverageArgs,
     sizeof(EllipseCoverageArgs) / sizeof(EllipseCoverageArgs[0])},
    {4, "hairlineLineEdgeCoverage", HairlineLineCoverageArgs,
     sizeof(HairlineLineCoverageArgs) / sizeof(HairlineLineCoverageArgs[0])},
    {5, "complexEllipseEdgeCoverage", ComplexEllipseCoverageArgs,
     sizeof(ComplexEllipseCoverageArgs) / sizeof(ComplexEllipseCoverageArgs[0])},
    {6, "nonAARRectCoverage", NonAARRectCoverageArgs,
     sizeof(NonAARRectCoverageArgs) / sizeof(NonAARRectCoverageArgs[0])},
    {7, "complexRRectCoverage", ComplexRRectCoverageArgs,
     sizeof(ComplexRRectCoverageArgs) / sizeof(ComplexRRectCoverageArgs[0])},
    {8, "hairlineQuadEdgeCoverage", HairlineQuadCoverageArgs,
     sizeof(HairlineQuadCoverageArgs) / sizeof(HairlineQuadCoverageArgs[0])},
};

}  // namespace

const ChainGeometryCoverageCall* ChainGeometryCoverageOf(uint8_t geometry) {
  for (const auto& call : GeometryCoverageCalls) {
    if (call.geometry == geometry) {
      return &call;
    }
  }
  return nullptr;
}

std::string ChainLeafMemberName(const char* base, int leaf) {
  return leaf == 0 ? std::string(base) : std::string(base) + "_" + std::to_string(leaf);
}

// The shared kind-to-op mapping and parameter admission for the unary pointwise operators: the op
// and the checks only — the values ride the upload-time readers off the node itself.
bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node, int* op) {
  if (node == nullptr || op == nullptr) {
    return false;
  }
  if (node->kind == AOTEffectKind::ConstColor) {
    auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
    if (parameters == nullptr || node->inputs.size() != 1) {
      return false;
    }
    *op = ChainISA::ConstColor;
    return true;
  }
  if (node->kind == AOTEffectKind::Blend) {
    auto parameters = std::get_if<AOTBlendParameters>(&node->parameters);
    if (parameters == nullptr || parameters->childType == 2 || node->inputs.size() != 2) {
      return false;
    }
    auto* srcNode = graph.nodeAt(node->inputs[0]);
    auto* dstNode = graph.nodeAt(node->inputs[1]);
    const AOTEffectNode* constNode = nullptr;
    if (srcNode != nullptr && srcNode->kind == AOTEffectKind::ConstColor) {
      constNode = srcNode;
    } else if (dstNode != nullptr && dstNode->kind == AOTEffectKind::ConstColor) {
      constNode = dstNode;
    } else {
      return false;
    }
    // The blend uploads the constant as-is (its input mode is not applied), which is only correct
    // when the constant does not depend on its input edge.
    if (!AOTConstColorIsInputIndependent(graph, constNode)) {
      return false;
    }
    *op = ChainISA::Blend;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorMatrix) {
    auto parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    *op = ChainISA::ColorMatrix;
    return true;
  }
  if (node->kind == AOTEffectKind::Luma) {
    auto parameters = std::get_if<ChainISA::LumaData>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    *op = ChainISA::Luma;
    return true;
  }
  if (node->kind == AOTEffectKind::AlphaThreshold) {
    auto parameters = std::get_if<ChainISA::AlphaThresholdData>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    *op = ChainISA::AlphaThreshold;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorSpaceXform) {
    auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
    if (parameters == nullptr || parameters->steps == nullptr) {
      return false;
    }
    *op = ChainISA::ColorSpaceXform;
    return true;
  }
  return false;
}

// Whether a texture node's parameters describe a leaf the chain kernel samples itself: a resolved
// plain leaf (RGBAAA included), a device-space leaf (sampled through DeviceLeafCoordMatrix), or a
// tiled leaf whose shader wrap modes the kernel's single-tap path implements. YUV planes need their
// own kernel.
bool IsChainTextureLeaf(const AOTTextureParameters& parameters) {
  if ((parameters.textureProxy == nullptr && !parameters.isIntermediate) || parameters.isYUV) {
    return false;
  }
  if (parameters.samplingKind == AOTTextureSamplingKind::Plain) {
    // The kernel splats an alpha-only leaf's .r after the RGBAAA merge, while the runtime reads
    // the merged .a; an RGBAAA image is never alpha-only (RGBAAAImage::MakeFrom), so the
    // combination is refused instead of implemented.
    return !(parameters.hasRGBAAA && parameters.isAlphaOnly);
  }
  if (parameters.hasRGBAAA) {
    return false;
  }
  if (parameters.samplingKind == AOTTextureSamplingKind::Device) {
    // An alpha-only source takes the same selector bits as a plain alpha-only leaf. The kernel,
    // like the runtime effect, uses the mapped coordinate without a perspective divide, and the
    // leaf keeps refusing projective matrices.
    return !parameters.hasPerspective;
  }
  if (parameters.samplingKind != AOTTextureSamplingKind::Tiled) {
    return false;
  }
  const auto& recipe = parameters.tiledRecipe;
  return recipe.has_value() && AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeX) &&
         AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeY);
}

bool HasShaderTiling(const AOTTextureParameters& parameters) {
  return parameters.samplingKind == AOTTextureSamplingKind::Tiled &&
         parameters.tiledRecipe.has_value() &&
         (parameters.tiledRecipe->shaderModeX != TiledTextureShaderMode::None ||
          parameters.tiledRecipe->shaderModeY != TiledTextureShaderMode::None);
}

bool LayoutChainGraph(const DrawIR& ir, const PassIR* pass, AOTChainGraphLayout* layout) {
  const auto& graph = ir.color;
  if (layout == nullptr || graph.nodeCount() < 2 || !graph.root().isValid() ||
      graph.root().index() >= graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }

  // The pass node set: the caller's planned one, or the whole-graph derivation (every color node
  // beyond the root, filtered to the root-reachable set when the graph carries a noise source —
  // the noise family's budgets cover the reachable space only).
  std::vector<bool> inPass(graph.nodeCount(), false);
  if (pass != nullptr) {
    if (pass->nodes.empty() || !pass->output.isValid() ||
        pass->output.index() >= graph.nodeCount()) {
      return false;
    }
    for (auto nodeID : pass->nodes) {
      if (nodeID.index() >= graph.nodeCount() || inPass[nodeID.index()]) {
        return false;
      }
      inPass[nodeID.index()] = true;
    }
    if (!inPass[pass->output.index()]) {
      return false;
    }
  } else {
    bool containsNoise = false;
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      containsNoise |= graph.nodeAt(AOTNodeID(index))->kind == AOTEffectKind::PerlinNoiseSource;
    }
    std::vector<bool> reachable(graph.nodeCount(), !containsNoise);
    if (containsNoise && !CollectReachableNodes(graph, graph.root(), &reachable)) {
      return false;
    }
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      inPass[index] = reachable[index];
    }
  }
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    if (inPass[index] && !TraitsOf(graph.nodeAt(AOTNodeID(index))->kind).chainFusable) {
      return false;
    }
  }

  // The merged coverage terms: one graph over one geometry-coverage unit, the analytic links
  // chained onto it and the mask fact reported for the binding table's mask slot.
  ChainCoverageGraph coverage = {};
  if (!ir.coverage.empty()) {
    if (!MergeCoverageTerms(ir.coverage, &coverage)) {
      return false;
    }
  }
  const size_t colorCount = graph.nodeCount();
  const size_t covCount = coverage.graph.nodeCount();
  if (coverage.coverageRoot.isValid() && coverage.coverageRoot.index() >= covCount) {
    return false;
  }

  // The merged node space: the color graph's nodes, then the merged coverage graph's — the same
  // space the schedule order derives, so node indices mean the same thing everywhere.
  std::vector<const AOTEffectNode*> nodes = {};
  nodes.reserve(colorCount + covCount);
  for (size_t index = 0; index < colorCount; ++index) {
    auto* node = graph.nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    nodes.push_back(node);
  }
  for (size_t index = 0; index < covCount; ++index) {
    auto* node = coverage.graph.nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    nodes.push_back(node);
  }
  const size_t nodeTotal = nodes.size();

  // The instruction order: texture leaves first (color pass, then coverage subtree — a leaf-owning
  // site must sit exactly where its leaf record starts), then the remaining nodes in index order. A
  // computed-input texture occupies both phases: a raw sampling site in the leading block and the
  // modulate instruction in the op phase.
  std::vector<size_t> slotOf(nodeTotal, SIZE_MAX);
  std::vector<size_t> rawSlotOf(nodeTotal, SIZE_MAX);
  std::vector<size_t> ordered = {};
  const auto isComputedTextureInput = [&](size_t combined) {
    auto* node = nodes[combined];
    if (node->kind != AOTEffectKind::TextureSource || node->inputs.empty()) {
      return false;
    }
    const size_t inputBase = combined < colorCount ? 0 : colorCount;
    const size_t inputIndex = inputBase + node->inputs[0].index();
    if (inputIndex >= nodeTotal) {
      return false;
    }
    return !TraitsOf(nodes[inputIndex]->kind).isInputDesignator();
  };
  const auto collectNode = [&](size_t combined, bool texturePhase) {
    if (nodes[combined]->kind == AOTEffectKind::TextureSource) {
      if (texturePhase) {
        rawSlotOf[combined] = ordered.size();
        ordered.push_back(combined);
        if (!isComputedTextureInput(combined)) {
          slotOf[combined] = rawSlotOf[combined];
        }
      } else if (slotOf[combined] == SIZE_MAX) {
        slotOf[combined] = ordered.size();
        ordered.push_back(combined);
      }
    } else if (!texturePhase) {
      slotOf[combined] = ordered.size();
      ordered.push_back(combined);
    }
  };
  for (bool texturePhase : {true, false}) {
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      // The designator-only input nodes (the white / opaque geometry inputs a xfer's children
      // lower) stay out of the schedule — their value rides on the input-edge designators, not on
      // a chain slot. A plan pass already excludes them from its node list; the whole-graph walk
      // sees every node, so the exclusion lands here.
      if (inPass[index] && !TraitsOf(graph.nodeAt(AOTNodeID(index))->kind).isInputDesignator()) {
        collectNode(index, texturePhase);
      }
    }
    for (size_t index = 1; index < covCount; ++index) {
      auto kind = nodes[colorCount + index]->kind;
      if (kind != AOTEffectKind::GeometryWhiteInput &&
          kind != AOTEffectKind::GeometryColorOpaqueInput) {
        collectNode(colorCount + index, texturePhase);
      }
    }
  }
  if (ordered.size() > MaxChainSlots) {
    return false;
  }

  // The per-kind structure, spelled straight onto the layout's sites: the operator, the mapped
  // input designators and the selector's small values. Parameter values ride the upload-time
  // readers, so nothing here copies them.
  std::vector<AOTChainCallSite> sites(ordered.size());
  bool hasGradient = false;
  bool noiseSeen = false;
  size_t colorSpaceXformCount = 0;
  layout->coordSourceMask = coverage.coverageLeafFromUVCoord ? 0u : ~0u;
  const auto mapInput = [&](size_t inputBase, AOTNodeID input) {
    auto* node = nodes[inputBase + input.index()];
    auto designator = static_cast<int>(TraitsOf(node->kind).designator);
    if (designator != 0) {
      return designator;
    }
    auto slot = slotOf[inputBase + input.index()];
    return slot == SIZE_MAX ? -2 : static_cast<int>(slot);
  };
  for (size_t index = 0; index < ordered.size(); ++index) {
    const size_t combined = ordered[index];
    auto node = nodes[combined];
    const size_t inputBase = combined < colorCount ? 0 : colorCount;
    for (auto input : node->inputs) {
      if (inputBase + input.index() >= (combined < colorCount ? colorCount : nodeTotal)) {
        return false;
      }
    }
    AOTChainCallSite& site = sites[index];
    site.node = node;
    switch (node->kind) {
      case AOTEffectKind::TextureSource: {
        auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
        if (parameters == nullptr) {
          return false;
        }
        if (parameters->isYUV) {
          // The multi-plane source: only the structural facts resolve here (the conversion matrix
          // and the limited-range flag are upload-time reads off the view).
          if (parameters->samplingKind != AOTTextureSamplingKind::Plain || inputBase != 0 ||
              parameters->hasPerspective) {
            return false;
          }
          site.op = ChainISA::YUV;
          layout->leafNodes.push_back(node);
          layout->leafNodes.push_back(node);
          layout->leafNodes.push_back(node);
          break;
        }
        if (!IsChainTextureLeaf(*parameters)) {
          return false;
        }
        if (index != rawSlotOf[combined]) {
          // Op-phase entry of a computed-input texture: the explicit modulate instruction. Its
          // input is the producing node's register; its source is the raw sample prefetched at the
          // leading slot.
          site.op = ChainISA::TexModulate;
          site.in0 = mapInput(inputBase, node->inputs[0]);
          site.texModulateSourceSlot = static_cast<int>(rawSlotOf[combined]);
          site.texModulateAlphaOnly = parameters->isAlphaOnly;
          if (site.in0 < 0 || site.texModulateSourceSlot >= MaxFusedAOTSamplers) {
            return false;
          }
          break;
        }
        site.op = ChainISA::Texture;
        const bool rawSamplingOnly = slotOf[combined] != index;
        if (!rawSamplingOnly) {
          // A texture fed directly by the geometry color folds the paint-alpha modulation into its
          // read; any other texture samples raw. A leaf in the coverage subtree modulates by the
          // coverage unit's alpha wherever it sits. An alpha-only leaf's modulation follows its
          // input environment: the color root multiplies by the full geometry color in one step, a
          // two-child blend operand by the opaque geometry RGB.
          const bool inputIsGeometryColor =
              !node->inputs.empty() && inputBase == 0 && node->inputs[0] == AOTNodeID(0);
          int modulate = inputIsGeometryColor ? 1 : 0;
          const bool inputIsCoverageUnit =
              combined >= colorCount && !node->inputs.empty() && node->inputs[0].index() == 0;
          const int modulateUnit = inputIsCoverageUnit ? 1 : 0;
          const int alphaOnly = parameters->isAlphaOnly ? 1 : 0;
          const bool inputIsOpaqueDesignator =
              !node->inputs.empty() && nodes[inputBase + node->inputs[0].index()]->kind ==
                                           AOTEffectKind::GeometryColorOpaqueInput;
          int modulateGeometryRGB = 0;
          int modulateFullInput = 0;
          if (alphaOnly != 0 && inputIsGeometryColor) {
            modulate = 0;
            modulateFullInput = 1;
          } else if (alphaOnly != 0 && inputIsOpaqueDesignator) {
            modulateGeometryRGB = 1;
          }
          site.textureFlags = modulate | (alphaOnly << 1) | (modulateUnit << 2) |
                              (modulateGeometryRGB << 3) | (modulateFullInput << 4);
        }
        if (coverage.coverageLeafFromUVCoord && inputBase != 0) {
          // Atlas text: the coverage leaf sources its coordinates from the maskCoord attribute
          // (the uvCoord slot), while color leaves and gradients keep the position source.
          layout->coordSourceMask |= 1u << static_cast<uint32_t>(layout->leafNodes.size());
        }
        if (HasShaderTiling(*parameters)) {
          ++layout->tiledRecipeCount;
          layout->tiledLeafSlots.insert(static_cast<int>(layout->leafNodes.size()));
        }
        if (parameters->samplingKind == AOTTextureSamplingKind::Device) {
          // Every color leaf may sample in device space through its own coordinate matrix; a
          // device-space coverage texture becomes the mask child instead.
          if (inputBase != 0) {
            return false;
          }
          layout->deviceLeafIndices.push_back(static_cast<int>(layout->leafNodes.size()));
        }
        layout->leafNodes.push_back(node);
        break;
      }
      case AOTEffectKind::PerlinNoiseSource: {
        auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
        if (parameters == nullptr || noiseSeen || inputBase != 0 ||
            parameters->permutationsView == nullptr || parameters->noiseView == nullptr) {
          return false;
        }
        noiseSeen = true;
        layout->noise = parameters;
        site.op = ChainISA::PerlinNoise;
        break;
      }
      case AOTEffectKind::ConstColor: {
        auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        site.op = ChainISA::ConstColor;
        site.inputMode = parameters->inputMode;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::ColorMatrix:
      case AOTEffectKind::Luma:
      case AOTEffectKind::AlphaThreshold:
      case AOTEffectKind::ColorSpaceXform: {
        if (node->inputs.size() != 1) {
          return false;
        }
        // The color-space parameters are one shared chain-wide block, so at most one transform
        // per pass (the same single-block budget the variant derivation reads).
        if (node->kind == AOTEffectKind::ColorSpaceXform &&
            ++colorSpaceXformCount > MaxChainColorSpaceXformSlots) {
          return false;
        }
        // The shared kind-to-op mapping and parameter admission, the same definition the walk
        // consumes; only the op and the checks matter here — the values ride the upload-time
        // readers off the node.
        if (!BuildPointwiseSlot(graph, node, &site.op)) {
          return false;
        }
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::Blend: {
        auto parameters = std::get_if<AOTBlendParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 2) {
          return false;
        }
        // Two-child blends lower their children with the opaque-alpha geometry input, which only
        // the color graph carries.
        if (parameters->childType == 2 && inputBase != 0) {
          return false;
        }
        site.op = ChainISA::Blend;
        site.blendMode = parameters->blendMode;
        site.multiplyInputAlpha = parameters->multiplyInputAlpha;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        site.in1 = mapInput(inputBase, node->inputs[1]);
        break;
      }
      case AOTEffectKind::InputOpaque: {
        if (node->inputs.size() != 1) {
          return false;
        }
        site.op = ChainISA::InputOpaque;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::MulAlpha: {
        if (node->inputs.size() != 2) {
          return false;
        }
        site.op = ChainISA::MulAlpha;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        site.in1 = mapInput(inputBase, node->inputs[1]);
        break;
      }
      case AOTEffectKind::RectCoverage: {
        auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        site.op = IsIdentityMat3RowMajor(parameters->deviceToLocal) ? ChainISA::AARectCoverage
                                                                    : ChainISA::LocalRectCoverage;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::RRectCoverage: {
        auto parameters = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        site.op = ChainISA::RRectCoverage;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::GradientSource: {
        auto parameters = std::get_if<AOTGradientParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        // Single-authority budget: the kernel carries one chain-wide gradient parameter block.
        if (hasGradient) {
          return false;
        }
        hasGradient = true;
        if (parameters->colorizerKind == 3) {
          if (parameters->lutProxy == nullptr) {
            return false;
          }
          layout->lutGradient = parameters;
        }
        site.op = ChainISA::Gradient;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      default:
        LOGE("Layout reject #35 kind=%d", static_cast<int>(node->kind));
        return false;
    }
    // An input edge whose producer is outside the pass: the expression omitted a node it depends
    // on. Texture, noise and YUV carry their flags in the inputs, so they are exempt.
    if (site.op != ChainISA::Texture && site.op != ChainISA::PerlinNoise &&
        site.op != ChainISA::YUV &&
        (site.in0 == -2 ||
         ((site.op == ChainISA::Blend || site.op == ChainISA::MulAlpha) && site.in1 == -2))) {
      return false;
    }
    if (!OpAssembles(site.op)) {
      return false;
    }
  }
  // The shader-tiled leaves' recipes are spelled per leaf by the assembled text (the tiled catalog
  // functions), so no tiling module or runtime loop is needed here.
  // The successful shapes' cross-node constraints: a LUT gradient shares the leaf budget with at
  // most one leaf, and the noise family's budgets cover two leaves beside its own lookup tables,
  // no gradient and no atlas glyph.
  const size_t leafCount = layout->leafNodes.size();
  if ((layout->lutGradient != nullptr && leafCount >= 2) ||
      (noiseSeen && (hasGradient || leafCount > 2 || coverage.coverageLeafFromUVCoord))) {
    return false;
  }

  // The coverage subtree's root slot: the subtree's root names the register the tail's
  // final-coverage expression reads, and the atlas glyph — true coverage for an alpha-only atlas —
  // reroutes through the clip-coverage channel instead. The analytic clip links chain onto the clip
  // channel in chain order, each link consuming the previous one's slot.
  const size_t rootIndex = slotOf[(pass != nullptr ? pass->output : graph.root()).index()];
  if (rootIndex == SIZE_MAX) {
    return false;
  }
  int coverageRootSlot = -1;
  int clipCoverageSlot = -1;
  int clipChainInput = -4;
  if (covCount != 0 && coverage.coverageRoot.isValid()) {
    const size_t covRootCombined = colorCount + coverage.coverageRoot.index();
    auto* covRootNode = nodes[covRootCombined];
    const bool rootIsBlend = covRootNode->kind == AOTEffectKind::Blend;
    const bool rootIsTexture = covRootNode->kind == AOTEffectKind::TextureSource;
    const bool rootIsGradient = covRootNode->kind == AOTEffectKind::GradientSource;
    const bool rootIsConstant = covRootNode->kind == AOTEffectKind::ConstColor;
    const bool rootIsAnalytic = covRootNode->kind == AOTEffectKind::RectCoverage ||
                                covRootNode->kind == AOTEffectKind::RRectCoverage;
    if (!rootIsBlend && !rootIsTexture && !rootIsGradient && !rootIsConstant && !rootIsAnalytic) {
      return false;
    }
    int coverageBlendCount = 0;
    bool rootConsumesUnit = false;
    for (size_t j = 1; j < covCount; ++j) {
      auto* node = nodes[colorCount + j];
      if (node->kind == AOTEffectKind::Blend) {
        ++coverageBlendCount;
      }
      for (auto input : node->inputs) {
        if (input.index() != 0) {
          continue;
        }
        if (colorCount + j == covRootCombined) {
          rootConsumesUnit = true;
          continue;
        }
        if (node->kind != AOTEffectKind::TextureSource &&
            node->kind != AOTEffectKind::GradientSource &&
            node->kind != AOTEffectKind::RectCoverage &&
            node->kind != AOTEffectKind::RRectCoverage) {
          return false;
        }
      }
    }
    // An analytic root consumes the previous analytic node rather than the unit, but the chain
    // still transmits the unit coverage transitively, so only analytic roots may omit a direct
    // unit input.
    if (coverageBlendCount != (rootIsBlend ? 1 : 0) || (!rootConsumesUnit && !rootIsAnalytic)) {
      return false;
    }
    auto covSlot = slotOf[covRootCombined];
    if (covSlot == SIZE_MAX) {
      return false;
    }
    coverageRootSlot = static_cast<int>(covSlot);
    if (coverage.coverageLeafFromUVCoord) {
      // Route the atlas subtree's root through the clip-coverage register so it rides the XP's
      // coverage input with the unpremultiplied source; a draw clip then chains on top of the
      // glyph.
      if (clipCoverageSlot >= 0) {
        return false;
      }
      clipCoverageSlot = coverageRootSlot;
      clipChainInput = coverageRootSlot;
      coverageRootSlot = -1;
    }
  }
  for (auto link : coverage.clipLinks) {
    auto slot = slotOf[colorCount + link.index()];
    if (slot == SIZE_MAX) {
      continue;
    }
    sites[slot].in0 = clipChainInput;
    clipChainInput = static_cast<int>(slot);
    clipCoverageSlot = static_cast<int>(slot);
  }

  layout->nodes = std::move(nodes);
  layout->colorCount = colorCount;
  layout->covCount = covCount;
  layout->slotOf = std::move(slotOf);
  layout->rawSlotOf = std::move(rawSlotOf);
  layout->ordered = std::move(ordered);
  layout->sites = std::move(sites);
  layout->rootIndex = rootIndex;
  layout->coverageRootSlot = coverageRootSlot;
  layout->clipCoverageSlot = clipCoverageSlot;
  layout->instructionCount = layout->ordered.size();
  layout->coverage = std::move(coverage);
  layout->coverageGraph =
      layout->coverage.graph.nodeCount() == 0 ? nullptr : &layout->coverage.graph;
  return true;
}

bool PlanChainProgramFromGraph(const DrawIR& ir, bool xpNeedsBlendPass, const Family& family,
                               const Variant& variant, const PrecompiledShaderCache* cache,
                               ChainProgramPlan* plan) {
  if (cache == nullptr || plan == nullptr) {
    return false;
  }
  AOTChainGraphLayout layout = {};
  if (!LayoutChainGraph(ir, nullptr, &layout)) {
    return false;
  }

  // The register assignment: the last-use recycler over the instruction inputs and the three
  // out-of-loop roots, after which every input edge holds either a designator or a result
  // register.
  std::vector<std::vector<int>> instructionInputs(layout.sites.size());
  for (size_t index = 0; index < layout.sites.size(); ++index) {
    for (int input : {layout.sites[index].in0, layout.sites[index].in1}) {
      if (input >= 0) {
        instructionInputs[index].push_back(input);
      }
    }
  }
  AOTChainRegisterAssignment assignment = {};
  if (!AllocateChainRegisters(instructionInputs, static_cast<int>(layout.rootIndex),
                              layout.coverageRootSlot, layout.clipCoverageSlot, MaxChainRegisters,
                              &assignment)) {
    return false;
  }
  for (size_t index = 0; index < layout.sites.size(); ++index) {
    if (layout.sites[index].in0 >= 0) {
      layout.sites[index].in0 =
          assignment.outRegister[static_cast<size_t>(layout.sites[index].in0)];
    }
    if (layout.sites[index].in1 >= 0) {
      layout.sites[index].in1 =
          assignment.outRegister[static_cast<size_t>(layout.sites[index].in1)];
    }
    layout.sites[index].outRegister = assignment.outRegister[index];
  }

  ChainWalkFrame frame = {};
  frame.sites = std::move(layout.sites);
  frame.tiledLeafSlots = layout.tiledLeafSlots;
  frame.rootRegister = assignment.rootRegister;
  frame.coverageRootRegister = assignment.coverageRootRegister;
  frame.clipCoverageRegister = assignment.clipCoverageRegister;
  frame.xpNeedsBlendPass = xpNeedsBlendPass;
  return WalkChainProgram(frame, family, variant, plan);
}

void AppendChainPlanKey(const ChainProgramPlan& plan, BytesKey* key) {
  // The calls in instruction order are the text's direct input: the operators, their argument
  // structure and the result registers spell the body, and the declaration tables are the calls'
  // closure over the variant (which the key already separates). The epilogue registers and the
  // interface facts follow, so a plan field the printer reads cannot miss from the key.
  key->write(static_cast<uint32_t>(plan.calls.size()));
  for (const auto& call : plan.calls) {
    key->write(static_cast<uint32_t>(call.op));
    key->write(static_cast<uint32_t>(call.outRegister));
    key->write(call.multiplyInputAlpha ? 1u : 0u);
    key->write(static_cast<uint32_t>(call.args.size()));
    for (const auto& arg : call.args) {
      key->write(static_cast<uint32_t>(arg.kind));
      key->write(static_cast<uint32_t>(arg.index));
    }
  }
  key->write(static_cast<uint32_t>(plan.rootRegister));
  key->write(static_cast<uint32_t>(plan.clipCoverageRegister));
  key->write(static_cast<uint32_t>(plan.coverageRootRegister));
  key->write(static_cast<uint32_t>(plan.hasXp));
  key->write(plan.xpNeedsBlendPass ? 1u : 0u);
  key->write(plan.gradientLutForm ? 1u : 0u);
  key->write(plan.hasMaskSampler ? 1u : 0u);
  key->write(static_cast<uint32_t>(plan.geometry));
  key->write(static_cast<uint32_t>(plan.leafSlots.size()));
  for (int leaf : plan.leafSlots) {
    key->write(static_cast<uint32_t>(leaf));
  }
  key->write(static_cast<uint32_t>(plan.tiledLeafSlots.size()));
  for (int leaf : plan.tiledLeafSlots) {
    key->write(static_cast<uint32_t>(leaf));
  }
}

}  // namespace tgfx
