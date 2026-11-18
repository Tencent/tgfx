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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainProgramEmit.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/shaders/Layout.h"
#include "gpu/shaders/Module.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/FragmentLibrary.h"

namespace tgfx {
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

// The pack entry's text copied out; an empty result means the entry is absent.
std::string PackText(const PrecompiledShaderCache* cache, uint32_t id) {
  const auto* blob = cache->findPackEntry(id);
  if (blob == nullptr) {
    return {};
  }
  return std::string(blob->data.begin(), blob->data.end());
}

bool ReplaceAll(std::string* text, const std::string& token, const std::string& value) {
  const auto position = text->find(token);
  if (position == std::string::npos) {
    return false;
  }
  text->replace(position, token.size(), value);
  return true;
}

const char* GlslTypeOf(FragmentType type) {
  switch (type) {
    case FragmentType::Float:
      return "float";
    case FragmentType::Float2:
      return "vec2";
    case FragmentType::Float3:
      return "vec3";
    case FragmentType::Float4:
      return "vec4";
    case FragmentType::Int:
      return "int";
    case FragmentType::Int4:
      return "ivec4";
    case FragmentType::Matrix3:
      return "mat3";
    case FragmentType::Matrix4:
      return "mat4";
  }
  return "vec4";
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

std::string LeafName(const char* base, int leaf) {
  return leaf == 0 ? std::string(base) : std::string(base) + "_" + std::to_string(leaf);
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

/**
 * The JIT's declaration handler, reduced to what the assembled chain reads: every uniform, varying
 * and sampler is registered as the emission that reads it runs, so the declarations are the
 * emission's closure — what the composition does not read does not exist. `uniform` returns the
 * member's spelling so the caller splices the same string into the declaration and the read.
 */
class ChainRegistry {
 public:
  const std::string& uniform(const std::string& name, FragmentType type, ChainBindingSource source,
                             int slot, int index = -1, uint16_t count = 1) {
    const auto found = memberIndex.find(name);
    if (found != memberIndex.end()) {
      return names[members[found->second].nameIndex];
    }
    Member member = {};
    member.nameIndex = names.size();
    names.push_back(name);
    member.type = type;
    member.count = count;
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
    cursor = offset + stride * count;
    memberIndex[name] = members.size();
    members.push_back(member);
    return names.back();
  }

  void varying(const std::string& name, FragmentType type) {
    if (!varyingNames.insert(name).second) {
      return;
    }
    varyings.push_back({name, type});
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

  std::string declarations() const {
    std::string text = "layout(std140) uniform FragmentUniformBlock {\n";
    for (const auto& member : members) {
      text += "  ";
      text += GlslTypeOf(member.type);
      text += " ";
      text += names[member.nameIndex];
      if (member.count > 1) {
        text += "[" + std::to_string(member.count) + "]";
      }
      text += ";\n";
    }
    text += "};\n";
    for (const auto& varying : varyings) {
      text += " in ";
      text += GlslTypeOf(varying.second);
      text += " " + varying.first + ";\n";
    }
    for (const auto& sampler : samplers) {
      text += " uniform sampler2D " + sampler.name + ";\n";
    }
    text += "layout(location = 0) out vec4 fragColor;\n";
    return text;
  }

  uint32_t blockSize() const {
    return (cursor + 15) / 16 * 16;
  }

  const std::vector<ChainBinding>& bindings() const {
    return entries;
  }

  const std::vector<ChainSamplerBinding>& samplerBindings() const {
    return samplers;
  }

 private:
  struct Member {
    size_t nameIndex = 0;
    FragmentType type = FragmentType::Float;
    uint16_t count = 1;
  };

  std::deque<std::string> names = {};
  std::vector<Member> members = {};
  std::map<std::string, size_t> memberIndex = {};
  std::vector<std::pair<std::string, FragmentType>> varyings = {};
  std::set<std::string> varyingNames = {};
  std::vector<ChainSamplerBinding> samplers = {};
  std::set<std::string> samplerNames = {};
  std::vector<ChainBinding> entries = {};
  uint32_t cursor = 0;
};

// The per-body state: the locals the slots write, and the designator usage the source block and the
// varying contract follow.
struct EmitState {
  std::string body = {};
  std::unordered_map<int, std::string> registers = {};
  int nextLocal = 0;
  bool geomColorUsed = false;
  bool opaqueColorUsed = false;
  bool unitCoverageUsed = false;
  bool needsSourceCoords = false;
};

// The fixed designators, as the source functions the wrapper declares (the interpreter's macros).
bool ResolveInput(int designator, EmitState* state, std::string* expression) {
  switch (designator) {
    case -1:
      *expression = "chainGeomColorSource()";
      state->geomColorUsed = true;
      return true;
    case -3:
      *expression = "chainUnitCoverageSource()";
      state->unitCoverageUsed = true;
      return true;
    case -4:
      *expression = "vec4(1.0)";
      return true;
    case -5:
      *expression = "chainOpaqueGeomColorSource()";
      state->geomColorUsed = true;
      state->opaqueColorUsed = true;
      return true;
    default:
      break;
  }
  if (designator < 0) {
    *expression = "vec4(0.0)";
    return true;
  }
  const auto found = state->registers.find(designator);
  if (found == state->registers.end()) {
    return false;
  }
  *expression = found->second;
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

// The gradient evaluator's named block: the members the packed text reads, registered only when the
// composition reaches the evaluator. The LUT branch indexes the leaf samplers, so it exists only on
// a variant that declares them.
void RegisterGradientMembers(ChainRegistry* registry, bool lutForm) {
  registry->uniform("GradientLayoutType", FragmentType::Int, ChainBindingSource::GradientLayoutType,
                    -1);
  registry->uniform("GradientColorizerKind", FragmentType::Int,
                    ChainBindingSource::GradientColorizerKind, -1);
  if (lutForm) {
    registry->uniform("GradientLUTLeaf", FragmentType::Int, ChainBindingSource::GradientLutLeaf,
                      -1);
  }
  registry->uniform("Gradient_leftBorder", FragmentType::Float4,
                    ChainBindingSource::GradientLeftBorder, -1);
  registry->uniform("Gradient_rightBorder", FragmentType::Float4,
                    ChainBindingSource::GradientRightBorder, -1);
  registry->uniform("Gradient_start", FragmentType::Float4, ChainBindingSource::GradientStart, -1);
  registry->uniform("Gradient_end", FragmentType::Float4, ChainBindingSource::GradientEnd, -1);
  registry->uniform("Gradient_scale01", FragmentType::Float4, ChainBindingSource::GradientScale01,
                    -1);
  registry->uniform("Gradient_bias01", FragmentType::Float4, ChainBindingSource::GradientBias01,
                    -1);
  registry->uniform("Gradient_scale23", FragmentType::Float4, ChainBindingSource::GradientScale23,
                    -1);
  registry->uniform("Gradient_bias23", FragmentType::Float4, ChainBindingSource::GradientBias23,
                    -1);
  registry->uniform("Gradient_threshold", FragmentType::Float,
                    ChainBindingSource::GradientThreshold, -1);
  registry->uniform("Gradient_bias", FragmentType::Float, ChainBindingSource::GradientBias, -1);
  registry->uniform("Gradient_scale", FragmentType::Float, ChainBindingSource::GradientScale, -1);
  registry->uniform("Gradient_thresholds1_7", FragmentType::Float4,
                    ChainBindingSource::GradientThresholds1_7, -1);
  registry->uniform("Gradient_thresholds9_13", FragmentType::Float4,
                    ChainBindingSource::GradientThresholds9_13, -1);
  registry->uniform("Gradient_scales", FragmentType::Float4, ChainBindingSource::GradientScales, -1,
                    -1, 8);
  registry->uniform("Gradient_biases", FragmentType::Float4, ChainBindingSource::GradientBiases, -1,
                    -1, 8);
}

void RegisterNoiseMembers(ChainRegistry* registry) {
  registry->uniform("Noise_frequencyStitch", FragmentType::Float4,
                    ChainBindingSource::NoiseFrequencyStitch, -1);
  registry->uniform("NoiseModeOctaves", FragmentType::Int4, ChainBindingSource::NoiseModeOctaves,
                    -1);
}

// The color-space transform's one chain-wide block, shared by every slot (the matcher admits one).
void RegisterColorSpaceMembers(ChainRegistry* registry) {
  registry->uniform("ChainCSFlags", FragmentType::Int, ChainBindingSource::CSFlags, -1);
  registry->uniform("ChainSrcTF0", FragmentType::Float4, ChainBindingSource::CSTransferSrc0, -1);
  registry->uniform("ChainSrcTF1", FragmentType::Float4, ChainBindingSource::CSTransferSrc1, -1);
  registry->uniform("ChainSrcTFType", FragmentType::Int, ChainBindingSource::CSTransferSrcType, -1);
  registry->uniform("ChainSrcOOTF", FragmentType::Float4, ChainBindingSource::CSOotfSrc, -1);
  registry->uniform("ChainColorXform", FragmentType::Matrix3, ChainBindingSource::CSColorXform, -1);
  registry->uniform("ChainDstOOTF", FragmentType::Float4, ChainBindingSource::CSOotfDst, -1);
  registry->uniform("ChainDstTF0", FragmentType::Float4, ChainBindingSource::CSTransferDst0, -1);
  registry->uniform("ChainDstTF1", FragmentType::Float4, ChainBindingSource::CSTransferDst1, -1);
  registry->uniform("ChainDstTFType", FragmentType::Int, ChainBindingSource::CSTransferDstType, -1);
}

// One leaf's prefetch, its declarations registered as the statement is spelled: the sampler, the
// coordinate varying and the four per-leaf records the plain fetch reads.
void AppendLeafPrefetch(ChainRegistry* registry, int leaf, std::string* prefetch) {
  const std::string suffix = std::to_string(leaf);
  registry->sampler("TextureSampler_" + suffix, ChainSamplerSource::Leaf, leaf);
  registry->varying("TransformedCoords_" + suffix, FragmentType::Float3);
  registry->uniform(LeafName("Subset", leaf), FragmentType::Float4, ChainBindingSource::LeafSubset,
                    -1, leaf);
  registry->uniform(LeafName("SubsetClampOn", leaf), FragmentType::Int,
                    ChainBindingSource::LeafSubsetClampOn, -1, leaf);
  registry->uniform(LeafName("AlphaStart", leaf), FragmentType::Float2,
                    ChainBindingSource::LeafAlphaStart, -1, leaf);
  registry->uniform(LeafName("HasRgbaaa", leaf), FragmentType::Int,
                    ChainBindingSource::LeafHasRgbaaa, -1, leaf);
  *prefetch += "  chainLeafTex[" + suffix + "] = chainPlainLeafFetch(TextureSampler_" + suffix +
               ", TransformedCoords_" + suffix + ", " + LeafName("Subset", leaf) + ", " +
               LeafName("AlphaStart", leaf) + ", " + LeafName("HasRgbaaa", leaf) + ", " +
               LeafName("SubsetClampOn", leaf) + ", " + suffix + ");\n";
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

bool EmitChainProgramGLSL(const ChainComposition& composition, const Family& family,
                          const Variant& variant, const PrecompiledShaderCache* cache,
                          std::string* code, ChainBindings* bindings) {
  if (code == nullptr || bindings == nullptr || cache == nullptr) {
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
  const std::string tail = PackText(cache, FragmentPackTail);
  if (tail.empty()) {
    return false;
  }
  // The xfer fact picks the resolved support entry: 1 reads the dst through the texture, 2 through
  // framebuffer fetch; 0 carries none.
  const int hasXp = XferDimValue(family, variant);
  std::string xpSupport = {};
  if (hasXp == 1) {
    xpSupport = PackText(cache, FragmentPackXpDstTexture);
  } else if (hasXp == 2) {
    xpSupport = PackText(cache, FragmentPackXpFramebufferFetch);
  } else if (hasXp != 0) {
    return false;
  }
  if (hasXp != 0 && xpSupport.empty()) {
    return false;
  }

  // The variant's declared sampler set is the authority on which profile carries leaves and the
  // mask; the emit declares only the ones the emitted text reads.
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

  ChainRegistry registry = {};
  EmitState state = {};
  std::set<int> leafSlots = {};
  for (size_t slotIndex = 0; slotIndex < composition.slots.size(); ++slotIndex) {
    const auto& slot = composition.slots[slotIndex];
    if (slot.op == ChainISA::None || slot.outRegister < 0) {
      continue;
    }
    std::string in0 = {};
    std::string in1 = {};
    if (!ResolveInput(slot.in0, &state, &in0) || !ResolveInput(slot.in1, &state, &in1)) {
      return false;
    }
    const auto* def = FindFragment(slot.op);
    if (def == nullptr) {
      return false;
    }
    bool paramOk = true;
    auto param = [&](uint16_t index) -> std::string {
      const auto& spec = def->params[index];
      ChainBindingSource source = ChainBindingSource::SlotConstColor;
      if (!SourceOfSemantic(spec.semantic, &source)) {
        paramOk = false;
        return {};
      }
      return registry.uniform(MemberName(spec.semantic, slotIndex), spec.type, source,
                              static_cast<int>(slotIndex));
    };
    const std::string local = "cg" + std::to_string(state.nextLocal);
    ++state.nextLocal;
    switch (slot.op) {
      case ChainISA::ConstColor:
        state.body += "  vec4 " + local + " = pmApplyConstColor(" + param(0) + ", " + in0 + ", " +
                      std::to_string(slot.inputMode) + ");\n";
        break;
      case ChainISA::ColorMatrix:
        state.body += "  vec4 " + local + " = pmApplyColorMatrix(" + in0 + ", " + param(0) + ", " +
                      param(1) + ");\n";
        break;
      case ChainISA::Luma:
        state.body += "  vec4 " + local + " = pmApplyLuma(" + in0 + ", " + param(0) + ");\n";
        break;
      case ChainISA::AlphaThreshold:
        state.body +=
            "  vec4 " + local + " = pmApplyAlphaThreshold(" + in0 + ", " + param(0) + ");\n";
        break;
      case ChainISA::Blend:
        state.body += "  vec4 " + local + " = xpBlendColors(" + in0 + ", " + in1 + ", " +
                      std::to_string(slot.blendMode) + ");\n";
        if (slot.multiplyInputAlpha) {
          state.geomColorUsed = true;
          state.body += "  " + local + " *= chainGeomColorSource().a;\n";
        }
        break;
      case ChainISA::Texture:
        // The emit owns the prefetch, so the slot only modulates the fetched leaf.
        state.geomColorUsed = true;
        state.unitCoverageUsed = true;
        leafSlots.insert(static_cast<int>(slotIndex));
        state.body += "  vec4 " + local + " = tgfxOpTexture(chainLeafTex[" +
                      std::to_string(slotIndex) + "], chainGeomColorSource(), " +
                      "chainUnitCoverageSource(), " + std::to_string(slot.selector & 0x1F) + ");\n";
        break;
      case ChainISA::TexModulate: {
        const int srcSlot = (slot.selector >> 16) & 0xF;
        leafSlots.insert(srcSlot);
        state.body += "  vec4 " + local + " = tgfxOpTexModulate(chainLeafTex[" +
                      std::to_string(srcSlot) + "], " + in0 + ", " +
                      std::to_string(slot.selector & 1) + ");\n";
        break;
      }
      case ChainISA::InputOpaque:
        state.body += "  vec4 " + local + " = vec4(" + in0 + ".rgb, 1.0);\n";
        break;
      case ChainISA::MulAlpha:
        state.body += "  vec4 " + local + " = " + in0 + " * " + in1 + ".a;\n";
        break;
      case ChainISA::AARectCoverage:
        state.body += "  vec4 " + local + " = tgfxOpAARectCoverage(" + in0 + ", " + param(0) +
                      ", gl_FragCoord.xy);\n";
        break;
      case ChainISA::LocalRectCoverage:
        state.body += "  vec4 " + local + " = tgfxOpLocalRectCoverage(" + in0 + ", " + param(0) +
                      ", " + param(1) + ", gl_FragCoord.xy);\n";
        break;
      case ChainISA::RRectCoverage:
        state.body += "  vec4 " + local + " = tgfxOpRRectCoverage(" + in0 + ", " + param(0) + ", " +
                      param(1) + ", " + param(2) + ", " + param(3) + ", " + param(4) +
                      ", gl_FragCoord.xy);\n";
        break;
      case ChainISA::ColorSpaceXform:
        RegisterColorSpaceMembers(&registry);
        state.body += "  vec4 " + local + " = chainApplyColorSpaceXform(" + in0 + ");\n";
        break;
      case ChainISA::Gradient:
        RegisterGradientMembers(&registry, gradientLutForm);
        state.needsSourceCoords = true;
        state.body += "  vec4 " + local + " = evalGradientSlot(" + in0 + ");\n";
        break;
      case ChainISA::PerlinNoise:
        RegisterNoiseMembers(&registry);
        state.needsSourceCoords = true;
        state.body += "  vec4 " + local + " = evalNoiseSlot();\n";
        break;
      default:
        return false;
    }
    if (!paramOk) {
      return false;
    }
    state.registers[slot.outRegister] = local;
    if (composition.rootRegister == slot.outRegister) {
      state.body += "  result = " + local + ";\n";
    } else if (composition.clipCoverageRegister == slot.outRegister) {
      state.body += "  clipRegister = " + local + ";\n";
    } else if (composition.coverageRootRegister == slot.outRegister) {
      state.body += "  coverageRootRegister = " + local + ";\n";
    }
  }
  if (state.body.empty() && !composition.slots.empty()) {
    return false;
  }

  // The prefetches: one per texture leaf the composition samples, in slot order.
  std::string prefetch = {};
  for (int leaf : leafSlots) {
    if (leaf < 0 || leaf >= 4) {
      return false;
    }
    AppendLeafPrefetch(&registry, leaf, &prefetch);
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
  if (state.needsSourceCoords) {
    registry.varying("SourceCoords", FragmentType::Float3);
  }

  // The source functions the body calls, each declaring the members and varyings it reads. The
  // tail's final-coverage expression reads the unit coverage source whenever no coverage subtree
  // replaced it.
  if (composition.coverageRootRegister < 0) {
    state.unitCoverageUsed = true;
  }
  std::string sources = {};
  const bool hasColorVarying = LayoutDeclaresVarying(layout, "vColor");
  std::string colorExpr = "ChainColorParams_Color";
  if (hasColorVarying) {
    registry.uniform("ChainColorParams_Color", FragmentType::Float4,
                     ChainBindingSource::GeometryColor, -1);
    registry.uniform("ChainColorParams_HasCommonColor", FragmentType::Int,
                     ChainBindingSource::GeometryHasCommonColor, -1);
    registry.varying("vColor", FragmentType::Float4);
    colorExpr = "(ChainColorParams_HasCommonColor != 0) ? ChainColorParams_Color : vColor";
  }
  if (state.geomColorUsed) {
    sources += "vec4 chainGeomColorSource() {\n  return " + colorExpr + ";\n}\n\n";
  }
  if (state.opaqueColorUsed) {
    sources += "vec4 chainOpaqueGeomColorSource() {\n  vec4 color = " + colorExpr +
               ";\n  return vec4(color.rgb, 1.0);\n}\n\n";
  }
  if (state.unitCoverageUsed) {
    registry.varying("vCoverage", FragmentType::Float);
    sources += "vec4 chainUnitCoverageSource() {\n  return vec4(vCoverage);\n}\n\n";
  }
  if (composition.xpNeedsBlendPass) {
    const std::string modulation = composition.coverageRootRegister >= 0 ? "1" : "0";
    sources += "vec4 chainXPSrcUnpremul(vec4 result, vec4 coverageRootRegister) {\n  return (" +
               modulation + " != 0 ? result * coverageRootRegister : result);\n}\n";
  }

  // The used ops' pack entries, each at most once: the pure fragments and the shared dependencies
  // the coverage fragments call into.
  const bool needsTexture = !leafSlots.empty();
  const bool needsAnalytic = usedOps.count(ChainISA::LocalRectCoverage) != 0 ||
                             usedOps.count(ChainISA::RRectCoverage) != 0;
  std::string pieces = {};
  auto appendPiece = [&](uint32_t id) {
    const std::string text = PackText(cache, id);
    if (text.empty()) {
      return false;
    }
    pieces += text + "\n";
    return true;
  };
  if (needsTexture && !appendPiece(FragmentPackDepTextureSwizzle)) {
    return false;
  }
  if (needsTexture && !appendPiece(FragmentPackDepTextureSampling)) {
    return false;
  }
  if (needsAnalytic && !appendPiece(FragmentPackDepAnalyticClipMath)) {
    return false;
  }
  if (!xpSupport.empty()) {
    pieces += xpSupport + "\n";
  }
  for (int op : usedOps) {
    if (op == ChainISA::Gradient) {
      continue;
    }
    const auto* def = FindFragment(op);
    if (def == nullptr || def->sourceFile == nullptr) {
      continue;
    }
    if (!appendPiece(0x40000000u | static_cast<uint32_t>(op))) {
      return false;
    }
  }
  if (hasGradient) {
    if (!appendPiece(gradientLutForm ? FragmentPackGradientLut : FragmentPackGradientSimple)) {
      return false;
    }
  }
  if (needsTexture) {
    if (!appendPiece(hasSubset ? FragmentPackLeafFetchStrict : FragmentPackLeafFetchPlain)) {
      return false;
    }
  }

  // The tail's placeholders: the clip coverage, the final coverage before the clip folds in, the
  // device mask and the output statement.
  const std::string clip = composition.clipCoverageRegister >= 0 ? "clipRegister" : "vec4(1.0)";
  const std::string coverage =
      composition.coverageRootRegister >= 0 ? "coverageRootRegister" : "chainUnitCoverageSource()";
  std::string deviceMaskText = {};
  if (hasMaskSampler) {
    if (hasLeafSamplers) {
      deviceMaskText =
          "  if (HasMaskTexture != 0) {\n"
          "    highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);\n"
          "    finalCoverage *= texture(MaskTextureSampler, maskCoord.xy).r;\n"
          "  }";
    } else {
      deviceMaskText =
          "  highp vec3 maskCoord = DeviceCoordMatrix * vec3(gl_FragCoord.xy, 1.0);\n"
          "  finalCoverage *= texture(MaskTextureSampler, maskCoord.xy).r;";
    }
  }
  std::string output = {};
  if (hasXp != 0 && composition.xpNeedsBlendPass) {
    output = "  fragColor = xpCompositeBlendPass(result, chainXPDst, finalCoverage, xpBlended);";
  } else if (hasXp != 0) {
    output = "  fragColor = applyPorterDuffXP(result, finalCoverage);";
  } else {
    output = "  fragColor = (result * finalCoverage);";
  }
  std::string tailText = tail;
  if (!ReplaceAll(&tailText, "__TGFX_CLIP_COVERAGE__", clip) ||
      !ReplaceAll(&tailText, "__TGFX_FINAL_COVERAGE__", coverage) ||
      !ReplaceAll(&tailText, "__TGFX_DEVICE_MASK__", deviceMaskText) ||
      !ReplaceAll(&tailText, "__TGFX_OUTPUT__", output)) {
    return false;
  }

  // The stored vertex stage's members: the block the stored text declares, filled from the same
  // geometry and program facts the interpreter's writer reads.
  RegisterVertexMember(bindings, layout, "RTAdjust", ChainBindingSource::RTAdjust);
  RegisterVertexMember(bindings, layout, "Matrix", ChainBindingSource::VertexMatrix);
  RegisterVertexMember(bindings, layout, "InTexSubsetMatrix",
                       ChainBindingSource::VertexTexSubsetMatrix);
  for (int leaf : leafSlots) {
    const int transform = leaf + static_cast<int>(leafTransformShift);
    RegisterVertexMember(bindings, layout, "CoordTransformMatrix_" + std::to_string(transform),
                         ChainBindingSource::CoordTransform, transform);
  }
  if (state.needsSourceCoords) {
    RegisterVertexMember(bindings, layout, "CoordTransformMatrix_0",
                         ChainBindingSource::CoordTransform, 0);
  }

  std::string xpGlobals = {};
  std::string xpLocals = {};
  if (hasXp != 0) {
    xpGlobals = "vec4 chainXPDst = vec4(0.0);\nvec4 chainXPSrc = vec4(0.0);\n";
    xpLocals = "  chainXPDst = xpReadDstColor();\n  vec4 xpBlended = vec4(0.0);\n";
  }
  const std::string blendPass =
      "  chainXPSrc = chainXPSrcUnpremul(result, coverageRootRegister);\n"
      "  xpBlended = xpBlendColors(chainXPSrc, chainXPDst, XPBlendMode);\n";
  *code = "#version 330\n#define TGFX_DECLARED_CHAIN 1\n" + registry.declarations() + "\n" +
          sources + pieces + "vec4 chainLeafTex[4];\n" + xpGlobals +
          "\nvoid main() {\n"
          "  vec4 result = vec4(0.0);\n"
          "  vec4 clipRegister = vec4(0.0);\n"
          "  vec4 coverageRootRegister = vec4(0.0);\n" +
          xpLocals + prefetch + state.body +
          (composition.xpNeedsBlendPass ? blendPass : std::string()) + tailText + "}\n";
  bindings->fragment = registry.bindings();
  bindings->samplers = registry.samplerBindings();
  bindings->leafVaryings = leafSlots;
  bindings->needsSourceCoords = state.needsSourceCoords;
  bindings->leafTransformShift = leafTransformShift;
  bindings->fragmentBlockSize = registry.blockSize();
  // The dump aid: with TGFX_ASSEMBLED_DUMP pointing at a directory, every assembled text lands
  // there as assembled_N.glsl for inspection. Read-only diagnostic, no effect on the pipeline.
  if (const char* dumpDir = std::getenv("TGFX_ASSEMBLED_DUMP")) {
    static std::atomic<int> dumpCount = {0};
    const int index = dumpCount.fetch_add(1);
    std::ofstream file(std::string(dumpDir) + "/assembled_" + std::to_string(index) + ".glsl",
                       std::ios::binary);
    file << *code;
  }
  return true;
}

bool AssembleChainVertexGLSL(const ChainBindings& bindings, const std::string& storedVs,
                             std::string* code) {
  if (code == nullptr || storedVs.empty()) {
    return false;
  }
  // The stored vertex text is the rect geometry's exact interface (attributes, the vertex block,
  // the geometry hook); the emit supplies the varying contract: keep a varying line exactly when
  // the composition reads it, and write those varyings in the chain hook.
  const std::string varyingHead = " out vec3 TransformedCoords_";
  std::string out = {};
  size_t cursor = 0;
  while (cursor < storedVs.size()) {
    const auto lineEnd = storedVs.find('\n', cursor);
    const std::string line = storedVs.substr(
        cursor, lineEnd == std::string::npos ? std::string::npos : lineEnd - cursor + 1);
    cursor = lineEnd == std::string::npos ? storedVs.size() : lineEnd + 1;
    if (line.rfind(varyingHead, 0) == 0) {
      const int leaf = std::atoi(line.c_str() + varyingHead.size());
      if (bindings.leafVaryings.count(leaf) == 0) {
        continue;
      }
    } else if (line.rfind(" out vec3 SourceCoords;", 0) == 0 && !bindings.needsSourceCoords) {
      continue;
    }
    out += line;
  }
  std::string hookBody = {};
  for (int leaf : bindings.leafVaryings) {
    const int transform = leaf + static_cast<int>(bindings.leafTransformShift);
    hookBody += "  TransformedCoords_" + std::to_string(leaf) + " = (CoordTransformMatrix_" +
                std::to_string(transform) + " * vec3(v.localCoord, 1.0)).xyz;\n";
  }
  if (bindings.needsSourceCoords) {
    hookBody += "  SourceCoords = (CoordTransformMatrix_0 * vec3(v.localCoord, 1.0)).xyz;\n";
  }
  const auto hookAt = out.find("void Chain_vertex(inout Vertex v) {");
  if (hookAt == std::string::npos) {
    return false;
  }
  const auto bodyAt = out.find('\n', hookAt) + 1;
  const auto hookEnd = out.find("}\n", bodyAt);
  if (hookEnd == std::string::npos) {
    return false;
  }
  out = out.substr(0, bodyAt) + hookBody + out.substr(hookEnd);
  *code = out;
  return true;
}

}  // namespace tgfx
