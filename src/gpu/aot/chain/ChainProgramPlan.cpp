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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainProgramPlan.h"
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include "core/utils/Log.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/ir/ChainLayoutMath.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
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
  (void)lutForm;
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

// The YUV source's members: the names match the generated registration (the signature's derived
// spelling), but each carries the operator's own slot, so the reader finds the slot's record. The
// three planes ride the leaf-sampler bindings at the slot's own index and the two above it; the
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

/// One planned call site as the shared walk consumes it: the operator, its two resolved inputs (a
/// designator or a result register), the result register, and the operator's small selector values.
/// Both producers — the slot route over the composition and the graph route over the draw's IR —
/// normalize their shape into this view, so the argument marshalling and the declaration closure
/// exist exactly once and the two routes cannot drift in the walk itself.
struct ChainCallSite {
  int op = ChainISA::None;
  int in0 = -2;
  int in1 = -2;
  int outRegister = -1;
  /// Blend's mode and selector bit; ConstColor's input mode; Texture's low selector bits;
  /// TexModulate's raw-sampling leaf and alpha-only flag.
  int blendMode = 0;
  bool multiplyInputAlpha = false;
  int inputMode = 0;
  int textureFlags = 0;
  int texModulateSourceSlot = -1;
  bool texModulateAlphaOnly = false;
};

/// The shared walk's frame: the sites in instruction order (dead instructions keep their place —
/// the numbering is the plan's member-naming input), the epilogue registers the tail reads, and the
/// xfer's blend-pass fact.
struct ChainWalkFrame {
  std::vector<ChainCallSite> sites = {};
  int rootRegister = 0;
  int coverageRootRegister = -1;
  int clipCoverageRegister = -1;
  bool xpNeedsBlendPass = false;
};

/**
 * The shared plan walk: resolves every argument against the frame's designators and registers,
 * registers every uniform, varying and sampler the body reads (the declaration closure), and
 * collects the interface facts the printer and the linker spell from. The walk is a pure function
 * of the frame and the variant, so the two routes that build the frame spell one plan.
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
  if (frame.coverageRootRegister < 0) {
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
  plan->rootRegister = frame.rootRegister;
  plan->clipCoverageRegister = frame.clipCoverageRegister;
  plan->coverageRootRegister = frame.coverageRootRegister;
  plan->xpNeedsBlendPass = frame.xpNeedsBlendPass;
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
  // The described slots normalize into the walk's frame: the selector's small values unpack into
  // their named fields, so the walk reads semantics instead of bit positions.
  ChainWalkFrame frame = {};
  frame.sites.reserve(composition.slots.size());
  for (const auto& slot : composition.slots) {
    ChainCallSite site = {};
    site.op = slot.op;
    site.in0 = slot.in0;
    site.in1 = slot.in1;
    site.outRegister = slot.outRegister;
    site.blendMode = slot.blendMode;
    site.multiplyInputAlpha = slot.multiplyInputAlpha;
    site.inputMode = slot.inputMode;
    site.textureFlags = slot.selector & 0x1F;
    site.texModulateSourceSlot = (slot.selector >> 16) & 0xF;
    site.texModulateAlphaOnly = (slot.selector & 1) != 0;
    frame.sites.push_back(site);
  }
  frame.rootRegister = composition.rootRegister;
  frame.coverageRootRegister = composition.coverageRootRegister;
  frame.clipCoverageRegister = composition.clipCoverageRegister;
  frame.xpNeedsBlendPass = composition.xpNeedsBlendPass;
  return WalkChainProgram(frame, family, variant, plan);
}

bool PlanChainProgramFromGraph(const DrawIR& ir, bool xpNeedsBlendPass, const Family& family,
                               const Variant& variant, const PrecompiledShaderCache* cache,
                               ChainProgramPlan* plan) {
  if (cache == nullptr || plan == nullptr) {
    return false;
  }
  // The rect-geometry slice: the same acceptance the composition walk runs, so the two routes
  // serve the same shapes.
  if (variant.geometry != 0) {
    return false;
  }
  const auto& graph = ir.color;
  if (graph.nodeCount() < 2 || !graph.root().isValid() ||
      graph.root().index() >= graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }

  // The pass node set, derived the way the slot pipeline's own planner derives it: every color node
  // beyond the root, filtered to the root-reachable set when the graph carries a noise source (the
  // noise family's budgets cover the reachable space only).
  bool containsNoise = false;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    containsNoise |= graph.nodeAt(AOTNodeID(index))->kind == AOTEffectKind::PerlinNoiseSource;
  }
  std::vector<bool> reachable(graph.nodeCount(), !containsNoise);
  if (containsNoise && !CollectReachableNodes(graph, graph.root(), &reachable)) {
    return false;
  }
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    if (reachable[index] && !TraitsOf(graph.nodeAt(AOTNodeID(index))->kind).chainFusable) {
      return false;
    }
  }
  std::vector<uint32_t> passNodes = {};
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (reachable[index] && !TraitsOf(node->kind).isInputDesignator()) {
      passNodes.push_back(index);
    }
  }

  // The merged node space: the color graph's nodes, then the merged coverage graph's — the same
  // space the slot pipeline's schedule merges, so node indices mean the same thing on both routes.
  ChainCoverageGraph coverage = {};
  const AOTEffectGraph* coverageGraph = nullptr;
  if (!ir.coverage.empty()) {
    if (!MergeCoverageTerms(ir.coverage, &coverage)) {
      return false;
    }
    coverageGraph = &coverage.graph;
  }
  const size_t colorCount = graph.nodeCount();
  const size_t covCount = coverageGraph != nullptr ? coverageGraph->nodeCount() : 0;
  if (coverageGraph != nullptr && coverage.coverageRoot.isValid() &&
      coverage.coverageRoot.index() >= covCount) {
    return false;
  }
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
    auto* node = coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    nodes.push_back(node);
  }
  const size_t nodeTotal = nodes.size();

  // The instruction order the slot pipeline's schedule derives: texture leaves first (color pass,
  // then coverage subtree — a leaf-owning slot must sit exactly where its leaf record starts), then
  // the remaining nodes in index order. A computed-input texture occupies both phases: a raw
  // sampling slot in the leading block and the modulate instruction in the op phase.
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
    for (auto nodeIndex : passNodes) {
      collectNode(nodeIndex, texturePhase);
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

  // The per-kind structure the slot encoders produce, spelled straight onto the walk's sites: the
  // operator, the mapped input designators and the selector's small values. Parameter values ride
  // the upload-time readers, so nothing here copies them.
  std::vector<ChainCallSite> sites(ordered.size());
  bool hasGradient = false;
  bool hasLutGradient = false;
  bool noiseSeen = false;
  size_t leafCount = 0;
  bool hasShaderTiledLeaf = false;
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
    ChainCallSite& site = sites[index];
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
          leafCount += 3;
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
        if (HasShaderTiling(*parameters)) {
          hasShaderTiledLeaf = true;
        }
        if (parameters->samplingKind == AOTTextureSamplingKind::Device && inputBase != 0) {
          return false;
        }
        ++leafCount;
        break;
      }
      case AOTEffectKind::PerlinNoiseSource: {
        auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
        if (parameters == nullptr || noiseSeen || inputBase != 0 ||
            parameters->permutationsView == nullptr || parameters->noiseView == nullptr) {
          return false;
        }
        noiseSeen = true;
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
        // The shared kind-to-op mapping and parameter admission, the same definition the YUV tail
        // consumes (BuildPointwiseSlot); only the op and the checks matter here — the values ride
        // the upload-time readers.
        AOTChainSlot slot = {};
        if (!BuildPointwiseSlot(graph, node, &slot)) {
          return false;
        }
        site.op = static_cast<int>(slot.op);
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
          hasLutGradient = true;
        }
        site.op = ChainISA::Gradient;
        site.in0 = mapInput(inputBase, node->inputs[0]);
        break;
      }
      default:
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
  // Shader-tiled leaves need the tiling module's runtime loops, which the pack does not spell.
  if (hasShaderTiledLeaf) {
    return false;
  }
  // The successful shapes' cross-node constraints the slot pipeline's profile resolution enforces:
  // a LUT gradient shares the leaf budget with at most one leaf, and the noise family's budgets
  // cover two leaves beside its own lookup tables, no gradient and no atlas glyph.
  if ((hasLutGradient && leafCount >= 2) ||
      (noiseSeen && (hasGradient || leafCount > 2 || coverage.coverageLeafFromUVCoord))) {
    return false;
  }

  // The coverage subtree's root slot, derived the way the encode stage derives it: the subtree's
  // root names the register the tail's final-coverage expression reads, and the atlas glyph — true
  // coverage for an alpha-only atlas — reroutes through the clip-coverage channel instead. The
  // analytic clip links chain onto the clip channel in chain order, each link consuming the
  // previous one's slot.
  const size_t rootIndex = slotOf[graph.root().index()];
  if (rootIndex == SIZE_MAX) {
    return false;
  }
  int coverageRootSlot = -1;
  int clipCoverageSlot = -1;
  int clipChainInput = -4;
  if (coverageGraph != nullptr && coverage.coverageRoot.isValid()) {
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

  // The register assignment: the same last-use recycler the slot pipeline runs, over the same
  // instruction inputs and the same three out-of-loop roots, after which every input edge holds
  // either a designator or a result register.
  std::vector<std::vector<int>> instructionInputs(sites.size());
  for (size_t index = 0; index < sites.size(); ++index) {
    for (int input : {sites[index].in0, sites[index].in1}) {
      if (input >= 0) {
        instructionInputs[index].push_back(input);
      }
    }
  }
  AOTChainRegisterAssignment assignment = {};
  if (!AllocateChainRegisters(instructionInputs, static_cast<int>(rootIndex), coverageRootSlot,
                              clipCoverageSlot, MaxChainRegisters, &assignment)) {
    return false;
  }
  for (size_t index = 0; index < sites.size(); ++index) {
    if (sites[index].in0 >= 0) {
      sites[index].in0 = assignment.outRegister[static_cast<size_t>(sites[index].in0)];
    }
    if (sites[index].in1 >= 0) {
      sites[index].in1 = assignment.outRegister[static_cast<size_t>(sites[index].in1)];
    }
    sites[index].outRegister = assignment.outRegister[index];
  }

  ChainWalkFrame frame = {};
  frame.sites = std::move(sites);
  frame.rootRegister = assignment.rootRegister;
  frame.coverageRootRegister = assignment.coverageRootRegister;
  frame.clipCoverageRegister = assignment.clipCoverageRegister;
  frame.xpNeedsBlendPass = xpNeedsBlendPass;
  return WalkChainProgram(frame, family, variant, plan);
}

namespace {

// The parity check's field-by-field comparison: the first differing field names itself (with the
// index it sits at), so the log line points at the drift instead of a bare inequality.
bool ChainProgramPlansEqual(const ChainProgramPlan& expected, const ChainProgramPlan& actual,
                            std::string* firstDifference) {
  const auto differ = [&](const std::string& field) {
    *firstDifference = field;
    return false;
  };
  if (expected.calls.size() != actual.calls.size()) {
    return differ("calls.size");
  }
  for (size_t index = 0; index < expected.calls.size(); ++index) {
    const auto& expectedCall = expected.calls[index];
    const auto& actualCall = actual.calls[index];
    if (expectedCall.op != actualCall.op) {
      return differ("calls[" + std::to_string(index) + "].op");
    }
    if (expectedCall.outRegister != actualCall.outRegister) {
      return differ("calls[" + std::to_string(index) + "].outRegister");
    }
    if (expectedCall.multiplyInputAlpha != actualCall.multiplyInputAlpha) {
      return differ("calls[" + std::to_string(index) + "].multiplyInputAlpha");
    }
    if (expectedCall.args.size() != actualCall.args.size()) {
      return differ("calls[" + std::to_string(index) + "].args.size");
    }
    for (size_t arg = 0; arg < expectedCall.args.size(); ++arg) {
      if (expectedCall.args[arg].kind != actualCall.args[arg].kind) {
        return differ("calls[" + std::to_string(index) + "].args[" + std::to_string(arg) +
                      "].kind");
      }
      if (expectedCall.args[arg].index != actualCall.args[arg].index) {
        return differ("calls[" + std::to_string(index) + "].args[" + std::to_string(arg) +
                      "].index");
      }
    }
  }
  if (expected.uniforms.size() != actual.uniforms.size()) {
    return differ("uniforms.size");
  }
  for (size_t index = 0; index < expected.uniforms.size(); ++index) {
    const auto& expectedMember = expected.uniforms[index];
    const auto& actualMember = actual.uniforms[index];
    if (expectedMember.name != actualMember.name) {
      return differ("uniforms[" + std::to_string(index) + "].name");
    }
    if (expectedMember.type != actualMember.type) {
      return differ("uniforms[" + std::to_string(index) + "].type");
    }
    if (expectedMember.count != actualMember.count) {
      return differ("uniforms[" + std::to_string(index) + "].count");
    }
    if (expectedMember.source != actualMember.source) {
      return differ("uniforms[" + std::to_string(index) + "].source");
    }
    if (expectedMember.slot != actualMember.slot) {
      return differ("uniforms[" + std::to_string(index) + "].slot");
    }
    if (expectedMember.index != actualMember.index) {
      return differ("uniforms[" + std::to_string(index) + "].index");
    }
    if (expectedMember.offset != actualMember.offset) {
      return differ("uniforms[" + std::to_string(index) + "].offset");
    }
    if (expectedMember.size != actualMember.size) {
      return differ("uniforms[" + std::to_string(index) + "].size");
    }
  }
  if (expected.varyings.size() != actual.varyings.size()) {
    return differ("varyings.size");
  }
  for (size_t index = 0; index < expected.varyings.size(); ++index) {
    if (expected.varyings[index].name != actual.varyings[index].name) {
      return differ("varyings[" + std::to_string(index) + "].name");
    }
    if (expected.varyings[index].type != actual.varyings[index].type) {
      return differ("varyings[" + std::to_string(index) + "].type");
    }
  }
  if (expected.samplers.size() != actual.samplers.size()) {
    return differ("samplers.size");
  }
  for (size_t index = 0; index < expected.samplers.size(); ++index) {
    if (expected.samplers[index].name != actual.samplers[index].name) {
      return differ("samplers[" + std::to_string(index) + "].name");
    }
    if (expected.samplers[index].source != actual.samplers[index].source) {
      return differ("samplers[" + std::to_string(index) + "].source");
    }
    if (expected.samplers[index].index != actual.samplers[index].index) {
      return differ("samplers[" + std::to_string(index) + "].index");
    }
  }
  if (expected.geomColorUsed != actual.geomColorUsed) {
    return differ("geomColorUsed");
  }
  if (expected.opaqueColorUsed != actual.opaqueColorUsed) {
    return differ("opaqueColorUsed");
  }
  if (expected.unitCoverageUsed != actual.unitCoverageUsed) {
    return differ("unitCoverageUsed");
  }
  if (expected.needsSourceCoords != actual.needsSourceCoords) {
    return differ("needsSourceCoords");
  }
  if (expected.hasColorVarying != actual.hasColorVarying) {
    return differ("hasColorVarying");
  }
  if (expected.hasSubset != actual.hasSubset) {
    return differ("hasSubset");
  }
  if (expected.hasLeafSamplers != actual.hasLeafSamplers) {
    return differ("hasLeafSamplers");
  }
  if (expected.hasMaskSampler != actual.hasMaskSampler) {
    return differ("hasMaskSampler");
  }
  if (expected.gradientLutForm != actual.gradientLutForm) {
    return differ("gradientLutForm");
  }
  if (expected.hasXp != actual.hasXp) {
    return differ("hasXp");
  }
  if (expected.rootRegister != actual.rootRegister) {
    return differ("rootRegister");
  }
  if (expected.clipCoverageRegister != actual.clipCoverageRegister) {
    return differ("clipCoverageRegister");
  }
  if (expected.coverageRootRegister != actual.coverageRootRegister) {
    return differ("coverageRootRegister");
  }
  if (expected.xpNeedsBlendPass != actual.xpNeedsBlendPass) {
    return differ("xpNeedsBlendPass");
  }
  if (expected.leafSlots != actual.leafSlots) {
    return differ("leafSlots");
  }
  if (expected.usedOps != actual.usedOps) {
    return differ("usedOps");
  }
  if (expected.bindings.vertex.size() != actual.bindings.vertex.size()) {
    return differ("bindings.vertex.size");
  }
  for (size_t index = 0; index < expected.bindings.vertex.size(); ++index) {
    const auto& expectedBinding = expected.bindings.vertex[index];
    const auto& actualBinding = actual.bindings.vertex[index];
    if (expectedBinding.source != actualBinding.source) {
      return differ("bindings.vertex[" + std::to_string(index) + "].source");
    }
    if (expectedBinding.slot != actualBinding.slot) {
      return differ("bindings.vertex[" + std::to_string(index) + "].slot");
    }
    if (expectedBinding.index != actualBinding.index) {
      return differ("bindings.vertex[" + std::to_string(index) + "].index");
    }
    if (expectedBinding.offset != actualBinding.offset) {
      return differ("bindings.vertex[" + std::to_string(index) + "].offset");
    }
    if (expectedBinding.size != actualBinding.size) {
      return differ("bindings.vertex[" + std::to_string(index) + "].size");
    }
  }
  if (expected.bindings.fragment.size() != actual.bindings.fragment.size()) {
    return differ("bindings.fragment.size");
  }
  for (size_t index = 0; index < expected.bindings.fragment.size(); ++index) {
    const auto& expectedBinding = expected.bindings.fragment[index];
    const auto& actualBinding = actual.bindings.fragment[index];
    if (expectedBinding.source != actualBinding.source) {
      return differ("bindings.fragment[" + std::to_string(index) + "].source");
    }
    if (expectedBinding.slot != actualBinding.slot) {
      return differ("bindings.fragment[" + std::to_string(index) + "].slot");
    }
    if (expectedBinding.index != actualBinding.index) {
      return differ("bindings.fragment[" + std::to_string(index) + "].index");
    }
    if (expectedBinding.offset != actualBinding.offset) {
      return differ("bindings.fragment[" + std::to_string(index) + "].offset");
    }
    if (expectedBinding.size != actualBinding.size) {
      return differ("bindings.fragment[" + std::to_string(index) + "].size");
    }
  }
  if (expected.bindings.samplers.size() != actual.bindings.samplers.size()) {
    return differ("bindings.samplers.size");
  }
  for (size_t index = 0; index < expected.bindings.samplers.size(); ++index) {
    if (expected.bindings.samplers[index].name != actual.bindings.samplers[index].name) {
      return differ("bindings.samplers[" + std::to_string(index) + "].name");
    }
    if (expected.bindings.samplers[index].source != actual.bindings.samplers[index].source) {
      return differ("bindings.samplers[" + std::to_string(index) + "].source");
    }
    if (expected.bindings.samplers[index].index != actual.bindings.samplers[index].index) {
      return differ("bindings.samplers[" + std::to_string(index) + "].index");
    }
  }
  if (expected.bindings.leafVaryings != actual.bindings.leafVaryings) {
    return differ("bindings.leafVaryings");
  }
  if (expected.bindings.needsSourceCoords != actual.bindings.needsSourceCoords) {
    return differ("bindings.needsSourceCoords");
  }
  if (expected.bindings.leafTransformShift != actual.bindings.leafTransformShift) {
    return differ("bindings.leafTransformShift");
  }
  if (expected.bindings.fragmentBlockSize != actual.bindings.fragmentBlockSize) {
    return differ("bindings.fragmentBlockSize");
  }
  return true;
}

}  // namespace

void CheckChainProgramPlanParity(const ChainComposition& composition, const DrawIR& ir,
                                 bool xpNeedsBlendPass, const Family& family,
                                 const Variant& variant, const PrecompiledShaderCache* cache) {
  // How many draws the two planners disagreed on. Non-zero is a route-drift signal, not a
  // statistic to tolerate; debug builds assert on the first disagreement.
  static std::atomic<uint64_t> failures = {0};
  ChainProgramPlan slotPlan = {};
  ChainProgramPlan graphPlan = {};
  const bool slotOk = PlanChainProgram(composition, family, variant, cache, &slotPlan);
  const bool graphOk =
      PlanChainProgramFromGraph(ir, xpNeedsBlendPass, family, variant, cache, &graphPlan);
  if (!slotOk || !graphOk) {
    const auto count = failures.fetch_add(1, std::memory_order_relaxed) + 1;
    LOGE("ChainPlanParity: route disagreement #%llu for variant %u: slot route %s, graph route %s",
         static_cast<unsigned long long>(count), variant.id(), slotOk ? "succeeded" : "declined",
         graphOk ? "succeeded" : "declined");
    DEBUG_ASSERT(false);
    return;
  }
  std::string difference = {};
  if (!ChainProgramPlansEqual(slotPlan, graphPlan, &difference)) {
    const auto count = failures.fetch_add(1, std::memory_order_relaxed) + 1;
    LOGE("ChainPlanParity: plan mismatch #%llu for variant %u at %s (%zu vs %zu calls)",
         static_cast<unsigned long long>(count), variant.id(), difference.c_str(),
         slotPlan.calls.size(), graphPlan.calls.size());
    DEBUG_ASSERT(false);
  }
}

}  // namespace tgfx
