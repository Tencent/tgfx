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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
#include "AOTChainBuilder.h"
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/AOTChainRegisterAllocator.h"
#include "gpu/AOTRouter.h"
#include "gpu/AOTShapeRules.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/DrawIR.h"
#include "gpu/aot/NodeContract.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AOTYUVChainProcessor.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {

// The chain kernel's tiled path implements the same single-tap wrap modes as tiled_sample.inc:
// Clamp(1), RepeatNearest/LinearNone(2,3), MirrorRepeat(6) and ClampToBorderNearest/Linear(7,8).
// The mipmap-repeat modes (4,5) need a 4-tap seam blend that the kernel does not provide.
bool AOTChainBuilder::IsChainCompatibleTiledMode(TiledTextureShaderMode mode) {
  return mode == TiledTextureShaderMode::None || mode == TiledTextureShaderMode::Clamp ||
         mode == TiledTextureShaderMode::RepeatNearestNone ||
         mode == TiledTextureShaderMode::RepeatLinearNone ||
         mode == TiledTextureShaderMode::MirrorRepeat ||
         mode == TiledTextureShaderMode::ClampToBorderNearest ||
         mode == TiledTextureShaderMode::ClampToBorderLinear;
}

namespace {

// Rebuilds the texture processor of one TextureSource node plus its binding record: a plain or
// tiled leaf sampling through its resolved sampler state, or the stand-in leaf of a device-space
// source. Only a plain leaf may carry the RGBAAA layout: its rebuilt TextureEffect keeps the
// alpha-area offset and writes it to the kernel's per-leaf AlphaStart / HasRgbaaa fields. The
// binding carries exactly the data the child TextureEffect was constructed from.
struct ChainLeafChild {
  PlacementPtr<FragmentProcessor> processor = nullptr;
  AOTBinding binding = {};
};

static ChainLeafChild BuildTextureLeafChild(BlockAllocator* allocator, const AOTEffectNode* node) {
  ChainLeafChild child = {};
  auto parameters = node != nullptr && node->kind == AOTEffectKind::TextureSource
                        ? std::get_if<AOTTextureParameters>(&node->parameters)
                        : nullptr;
  if (parameters == nullptr ||
      (parameters->hasRGBAAA && parameters->samplingKind != AOTTextureSamplingKind::Plain)) {
    return child;
  }
  child.binding.kind = AOTBinding::Kind::Leaf;
  child.binding.textureProxy = parameters->textureProxy;
  if (parameters->samplingKind == AOTTextureSamplingKind::Device) {
    // The chain kernel samples this leaf at its DeviceLeafCoordMatrix * gl_FragCoord (see
    // AOTChainDeviceLeaf), so the child only binds the texture with the sampler state of a
    // DeviceSpaceTextureEffect (clamp, linear, no mipmaps). Its coordinate transform is unused,
    // and the unbounded sample area makes the kernel's subset clamp a no-op, as the runtime effect
    // samples unclamped.
    SamplingArgs args = {TileMode::Clamp, TileMode::Clamp,
                         SamplingOptions(FilterMode::Linear, MipmapMode::None),
                         SrcRectConstraint::Fast};
    args.sampleArea = Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f);
    auto uvMatrix = Matrix::I();
    child.processor = TextureEffect::Make(allocator, parameters->textureProxy, args, &uvMatrix);
    child.binding.args = args;
    child.binding.uvMatrix = uvMatrix;
    return child;
  }
  if (parameters->samplingKind == AOTTextureSamplingKind::Tiled) {
    const auto& recipe = parameters->tiledRecipe;
    if (!recipe.has_value() || !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeX) ||
        !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeY)) {
      return child;
    }
    // The hardware sampler performs all tiling (wrap modes and clamp-to-border), so the leaf is a
    // plain TextureEffect carrying the resolved sampler state. The huge sample area makes the
    // chain kernel's always-on Subset clamp a no-op, which the wrap/border modes rely on.
    SamplingOptions sampling(recipe->hardwareSampler.minFilterMode,
                             recipe->hardwareSampler.magFilterMode,
                             recipe->hardwareSampler.mipmapMode);
    SamplingArgs args = {recipe->hardwareSampler.tileModeX, recipe->hardwareSampler.tileModeY,
                         sampling, SrcRectConstraint::Fast};
    args.sampleArea = Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f);
    auto uvMatrix = parameters->uvMatrix;
    child.processor = TextureEffect::Make(allocator, parameters->textureProxy, args, &uvMatrix);
    child.binding.args = args;
    child.binding.uvMatrix = uvMatrix;
    return child;
  }
  SamplingOptions sampling(parameters->samplerState.minFilterMode,
                           parameters->samplerState.magFilterMode,
                           parameters->samplerState.mipmapMode);
  SamplingArgs args = {parameters->samplerState.tileModeX, parameters->samplerState.tileModeY,
                       sampling, parameters->constraint};
  args.sampleArea = parameters->subset;
  auto uvMatrix = parameters->uvMatrix;
  child.processor = TextureEffect::MakeRGBAAA(allocator, parameters->textureProxy, args,
                                              parameters->alphaStart, &uvMatrix);
  child.binding.args = args;
  child.binding.uvMatrix = uvMatrix;
  child.binding.alphaStart = parameters->alphaStart;
  return child;
}

static bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node,
                               AOTPointwiseSlot* slot);

// The operator slots of a linear pass: every node after the pass's source, in order.
static bool BuildLinearSlots(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                             std::vector<AOTPointwiseSlot>* slots) {
  slots->reserve(pass.nodes.size() - 1);
  for (size_t index = 1; index < pass.nodes.size(); ++index) {
    AOTPointwiseSlot slot = {};
    if (!BuildPointwiseSlot(graph, graph.nodeAt(pass.nodes[index]), &slot)) {
      return false;
    }
    slots->push_back(slot);
  }
  return true;
}

static bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node,
                               AOTPointwiseSlot* slot) {
  if (node == nullptr || slot == nullptr) {
    return false;
  }
  if (node->kind == AOTEffectKind::ConstColor) {
    auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
    if (parameters == nullptr || node->inputs.size() != 1) {
      return false;
    }
    slot->type = AOTPointwiseOpType::ConstColor;
    slot->constColor = *parameters;
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
    auto constParams = std::get_if<AOTConstColorParameters>(&constNode->parameters);
    // The blend slot uploads the constant as-is (its input mode is not applied), which is only
    // correct when the constant does not depend on its input edge.
    if (constParams == nullptr || !AOTConstColorIsInputIndependent(graph, constNode)) {
      return false;
    }
    slot->type = AOTPointwiseOpType::Blend;
    slot->blend = *parameters;
    slot->constColor = *constParams;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorMatrix) {
    auto parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::ColorMatrix;
    slot->colorMatrix = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::Luma) {
    auto parameters = std::get_if<AOTLumaParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::Luma;
    slot->luma = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::AlphaThreshold) {
    auto parameters = std::get_if<AOTAlphaThresholdParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::AlphaThreshold;
    slot->alphaThreshold = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorSpaceXform) {
    auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
    if (parameters == nullptr || parameters->steps == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::ColorSpaceXform;
    slot->colorSpaceXform = *parameters;
    return true;
  }
  return false;
}

// Maps a DAG input edge onto a chain slot index. GeometryColor is not a slot: it maps to -1, the
// geometry color. GeometryCoverage (the coverage subtree's unit input) always maps to -3, the
// true GP coverage: the kernel replaces the plain vCoverage modulation with the coverage root's
// value, so the chain must inject the GP coverage exactly once at the unit, wherever the unit is
// consumed — a chain whose first analytic node reads opaque white instead silently drops the GP
// coverage at AA edges. Opaque-white inputs are explicit GeometryWhiteInput nodes (single-child
// blend operands), never the coverage unit. Anything unmapped is -2, which callers treat as a
// build error.
static int MapChainInput(const std::vector<const AOTEffectNode*>& nodes,
                         const std::vector<size_t>& slotOf, size_t nodeIndex) {
  auto* node = nodes[nodeIndex];
  if (node->kind == AOTEffectKind::GeometryColor) {
    return -1;
  }
  if (node->kind == AOTEffectKind::GeometryCoverage) {
    return -3;
  }
  if (node->kind == AOTEffectKind::GeometryColorOpaqueInput) {
    return -5;
  }
  if (node->kind == AOTEffectKind::GeometryWhiteInput) {
    return -4;
  }
  auto slot = slotOf[nodeIndex];
  return slot == SIZE_MAX ? -2 : static_cast<int>(slot);
}

// Whether a Matrix::get9 row-major array is the identity matrix.
static bool IsIdentityMatrix(const std::array<float, 9>& values) {
  return values[0] == 1.0f && values[1] == 0.0f && values[2] == 0.0f && values[3] == 0.0f &&
         values[4] == 1.0f && values[5] == 0.0f && values[6] == 0.0f && values[7] == 0.0f &&
         values[8] == 1.0f;
}

// Whether a texture node's parameters describe a leaf the chain kernel samples itself: a resolved
// plain leaf (RGBAAA included), a device-space leaf (sampled through DeviceLeafCoordMatrix), or a
// tiled leaf whose shader wrap modes the kernel's single-tap path implements. YUV planes need their
// own kernel.
static bool IsChainTextureLeaf(const AOTTextureParameters& parameters) {
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
    // An alpha-only source takes the same slot selector bits as a plain alpha-only leaf. The
    // kernel, like the runtime effect, uses the mapped coordinate without a perspective divide,
    // and the leaf keeps refusing projective matrices.
    return !parameters.hasPerspective;
  }
  if (parameters.samplingKind != AOTTextureSamplingKind::Tiled) {
    return false;
  }
  const auto& recipe = parameters.tiledRecipe;
  return recipe.has_value() && AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeX) &&
         AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeY);
}

static bool HasShaderTiling(const AOTTextureParameters& parameters) {
  return parameters.samplingKind == AOTTextureSamplingKind::Tiled &&
         parameters.tiledRecipe.has_value() &&
         (parameters.tiledRecipe->shaderModeX != TiledTextureShaderMode::None ||
          parameters.tiledRecipe->shaderModeY != TiledTextureShaderMode::None);
}

// The shared state of the chain-layout pipeline (see PlanChainLayout): each stage fills part of it
// and reads what the earlier stages produced, so no shape rule is ever re-derived.
struct ChainBuildContext {
  // Pipeline inputs.
  const AOTEffectGraph* graph = nullptr;
  const AOTPassDescriptor* pass = nullptr;
  // The analytic coverage terms' links, as node IDs in the coverage graph, in chain order; they
  // are encoded as ordinary slots and rewired onto the clip-coverage channel by stage seven.
  const std::vector<AOTNodeID>* clipLinks = nullptr;
  const AOTEffectGraph* coverageGraph = nullptr;
  AOTNodeID coverageRoot = AOTNodeID();
  bool coverageLeafFromUVCoord = false;
  bool checkKernel = false;
  AOTChainCompile* layout = nullptr;
  // Stage results, consumed by the later stages.
  std::vector<const AOTEffectNode*> nodes = {};
  size_t colorCount = 0;
  size_t covCount = 0;
  std::vector<bool> inPass = {};
  std::vector<size_t> slotOf = {};
  std::vector<size_t> rawSlotOf = {};
  std::vector<size_t> ordered = {};
  std::vector<AOTChainSlot> slots = {};
  bool hasGradient = false;
  AOTResourceProfile finalProfile = AOTResourceProfile::Automatic;
  size_t rootIndex = 0;
  int coverageRootSlot = -1;
  int clipCoverageSlot = -1;
  int clipChainInput = -4;
  // Stage-three budget counters carried across the per-kind encoders.
  size_t rectCoverageSlots = 0;
  size_t localRectCoverageSlots = 0;
  AOTChainRegisterAssignment assignment = {};
};

// Maps one op input edge onto its chain register/designator, in the input's own node space.
static int MapChainOpInput(const ChainBuildContext* ctx, size_t inputBase, AOTNodeID input) {
  return MapChainInput(ctx->nodes, ctx->slotOf, inputBase + input.index());
}

// Stage one: merges the color and coverage node spaces, and admits the pass itself — non-empty
// with a valid output, every node listed exactly once, and a chain kernel when the caller asked
// for that check.
static bool MergeNodeSpaces(ChainBuildContext* ctx) {
  const auto& pass = *ctx->pass;
  if (ctx->layout == nullptr || pass.nodes.empty() || !pass.output.isValid() ||
      (ctx->checkKernel && !IsAOTPointwiseDAGKernel(pass.kernel))) {
    return false;
  }
  // Combined node space: color-graph nodes occupy [0, colorCount), coverage-graph nodes follow.
  ctx->colorCount = ctx->graph->nodeCount();
  ctx->covCount = ctx->coverageGraph != nullptr ? ctx->coverageGraph->nodeCount() : 0;
  if (ctx->coverageGraph != nullptr && ctx->coverageRoot.isValid() &&
      ctx->coverageRoot.index() >= ctx->covCount) {
    return false;
  }
  ctx->nodes = {};
  ctx->nodes.reserve(ctx->colorCount + ctx->covCount);
  for (size_t index = 0; index < ctx->colorCount; ++index) {
    auto* node = ctx->graph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    ctx->nodes.push_back(node);
  }
  for (size_t index = 0; index < ctx->covCount; ++index) {
    auto* node = ctx->coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    ctx->nodes.push_back(node);
  }
  // Every pass node must exist and appear once: a duplicate would encode the same node twice and
  // leave the first copy's consumers reading a stale register.
  ctx->inPass.assign(ctx->colorCount, false);
  for (auto nodeID : pass.nodes) {
    if (nodeID.index() >= ctx->colorCount || ctx->inPass[nodeID.index()]) {
      return false;
    }
    ctx->inPass[nodeID.index()] = true;
  }
  return pass.output.index() < ctx->colorCount && ctx->inPass[pass.output.index()];
}

// A texture whose input is a computed node (not one of the designators): its sampling needs no
// computed value, but its modulation must follow the producer, so the node occupies a raw
// sampling slot in the leading block plus a TEX_MODULATE slot in the op phase.
static bool HasComputedTextureInput(const ChainBuildContext* ctx, size_t combined) {
  auto* node = ctx->nodes[combined];
  if (node->kind != AOTEffectKind::TextureSource || node->inputs.empty()) {
    return false;
  }
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  const size_t inputIndex = inputBase + node->inputs[0].index();
  if (inputIndex >= ctx->nodes.size()) {
    return false;
  }
  auto* inputNode = ctx->nodes[inputIndex];
  return !IsInputDesignator(inputNode->kind);
}

static void CollectChainNode(ChainBuildContext* ctx, size_t combined, bool texturePhase) {
  if (ctx->nodes[combined]->kind == AOTEffectKind::TextureSource) {
    if (texturePhase) {
      ctx->rawSlotOf[combined] = ctx->ordered.size();
      ctx->ordered.push_back(combined);
      if (!HasComputedTextureInput(ctx, combined)) {
        ctx->slotOf[combined] = ctx->rawSlotOf[combined];
      }
    } else if (ctx->slotOf[combined] == SIZE_MAX) {
      ctx->slotOf[combined] = ctx->ordered.size();
      ctx->ordered.push_back(combined);
    }
  } else if (!texturePhase) {
    ctx->slotOf[combined] = ctx->ordered.size();
    ctx->ordered.push_back(combined);
  }
}

// Stage two: texture leaves first (color pass, then coverage subtree), then the remaining nodes
// in topological order (color ops, then coverage ops). The two subtrees never reference each
// other, so this layout keeps every input ahead of its consumer. A computed-input texture
// appears in both phases: the texture phase holds its raw sampling slot, the op phase its
// TEX_MODULATE instruction (slotOf points there, so consumers reference the modulated value).
// Index 0 of the coverage graph is the GeometryCoverage unit, which never becomes a slot.
// Designator-only input nodes (white / opaque geometry inputs lowered by xfer children) stay out
// too: their value rides on the input-edge designators, not on a chain slot.
static void ScheduleChainSlots(ChainBuildContext* ctx) {
  ctx->slotOf.assign(ctx->nodes.size(), SIZE_MAX);
  // For a computed-input texture node, the leading raw-sampling slot's position (sampler
  // binding); the node's value for consumers is the topologically-ordered TEX_MODULATE slot
  // tracked in slotOf.
  ctx->rawSlotOf.assign(ctx->nodes.size(), SIZE_MAX);
  ctx->ordered = {};
  ctx->ordered.reserve(ctx->pass->nodes.size() + ctx->covCount);
  for (bool texturePhase : {true, false}) {
    for (auto nodeID : ctx->pass->nodes) {
      CollectChainNode(ctx, nodeID.index(), texturePhase);
    }
    for (size_t index = 1; index < ctx->covCount; ++index) {
      auto kind = ctx->nodes[ctx->colorCount + index]->kind;
      if (kind != AOTEffectKind::GeometryWhiteInput &&
          kind != AOTEffectKind::GeometryColorOpaqueInput) {
        CollectChainNode(ctx, ctx->colorCount + index, texturePhase);
      }
    }
  }
}

// Stage three's per-kind encoders: each turns one scheduled node into its chain slot and
// registers the layout side data it carries (texture leaves, the LUT gradient, the noise source).
// The dispatch rows are indexed by AOTEffectKind and pinned to the enum's size, so adding a kind
// means writing one encoder and one row — a missing row fails the build. A null row is a kind that
// never schedules: the graph roots and the input designators stay out of the ordered set.
using ChainNodeEncoder = bool (*)(ChainBuildContext* ctx, size_t combined, size_t slotIndex);

static bool EncodeTextureSourceSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto* layout = ctx->layout;
  auto& slot = ctx->slots[index];
  auto* parameters = std::get_if<AOTTextureParameters>(&node->parameters);
  if (parameters == nullptr || !IsChainTextureLeaf(*parameters)) {
    return false;
  }
  if (index != ctx->rawSlotOf[combined]) {
    // Op-phase entry of a computed-input texture: the explicit modulate instruction.
    // Its input is the producing node's register (any computed node — an upstream
    // texture, a gradient, an operator), and its source is the raw sample prefetched
    // into chainLeafTex at the leading slot. The two modulations mirror the runtime
    // TextureEffect readback: alpha-only is sample.a * inputColor, RGBA is
    // sample * inputColor.a.
    slot.op = AOTChainOp::TexModulate;
    slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
    slot.texModulateSourceSlot = static_cast<int>(ctx->rawSlotOf[combined]);
    slot.texModulateAlphaOnly = parameters->isAlphaOnly ? 1 : 0;
    // A negative producer is a designator after all (HasComputedTextureInput disagreed
    // with the mapping) or is not part of this pass; both are build errors.
    if (slot.in0 < 0 || slot.texModulateSourceSlot >= MaxFusedAOTSamplers) {
      return false;
    }
    return true;
  }
  slot.op = AOTChainOp::Texture;
  // Texture-phase entry of a computed-input texture is raw sampling only: the modulation
  // lives in the op-phase TEX_MODULATE slot, so no flags here, and the result register
  // stays dead because only the modulate instruction reads the prefetched sample.
  const bool rawSamplingOnly = ctx->slotOf[combined] != index;
  if (!rawSamplingOnly) {
    // A texture fed directly by the geometry color is a color source and gets the paint-alpha
    // modulation folded into its read (as the runtime's SrcIn wrap does). Any other texture —
    // a coverage mask or a blend operand — must sample raw, matching the runtime emission.
    const bool inputIsGeometryColor =
        !node->inputs.empty() && inputBase == 0 && node->inputs[0] == AOTNodeID(0);
    slot.textureModulate = inputIsGeometryColor ? 1 : 0;
    // A leaf in the coverage subtree modulates by the coverage unit's alpha (bit 2) whenever
    // its input IS the unit — root or not. The runtime coverage-FP readback (tex * input.a)
    // applies wherever the mask sits in the subtree, so the modulation must not depend on
    // which node consumes it (: a non-root mask dropped the GP coverage; with GP
    // coverage q=.5, mask m=.5 and an inner rect, the chain produced m*r instead of q*m*r).
    // The unit is the coverage graph's node 0 (addGeometryCoverage runs first), so within
    // the coverage graph (combined >= colorCount) an input index of 0 is exactly the unit.
    const bool inputIsCoverageUnit =
        combined >= ctx->colorCount && !node->inputs.empty() && node->inputs[0].index() == 0;
    if (inputIsCoverageUnit) {
      slot.textureModulateUnit = 1;
    }
    // Alpha-only leaves (e.g. shape masks) need the kernel to splat .r into all channels; the
    // raw sample would otherwise read alpha as constant 1. The flag comes from the lowered
    // parameters, which record the same proxy property the rebuilt leaf reports.
    slot.textureAlphaOnly = parameters->isAlphaOnly ? 1 : 0;
    // The runtime alpha-only readback is sample.a * inputColor, so the leaf's input
    // environment decides the modulation shape — expressed as ONE complete operation per
    // shape: the color root multiplies by the full geometry color in a single step (bit 4;
    // the RGB is already premultiplied, so one multiply carries both halves — the former
    // bit0+bit3 pair applied the paint alpha to the RGB twice), and a two-child blend
    // operand — whose input node is the opaque geometry designator — multiplies by
    // (geom.rgb, 1.0) (bit 3) because the xfer emission feeds each child
    // vec4(inputColor.rgb, 1.0). White inputs (single-child operands, folded masks) and
    // coverage leaves keep the raw splat, matching the runtime emission.
    bool inputIsOpaqueDesignator =
        !node->inputs.empty() && ctx->nodes[inputBase + node->inputs[0].index()]->kind ==
                                     AOTEffectKind::GeometryColorOpaqueInput;
    if (slot.textureAlphaOnly != 0 && inputIsGeometryColor) {
      slot.textureModulate = 0;
      slot.textureModulateFullInput = 1;
    } else if (slot.textureAlphaOnly != 0 && inputIsOpaqueDesignator) {
      slot.textureModulateGeometryRGB = 1;
    }
  }
  if (ctx->coverageLeafFromUVCoord && inputBase != 0) {
    // Atlas text: the coverage leaf sources its coordinates from the maskCoord attribute
    // (the uvCoord slot), while color leaves and gradients keep the position source.
    layout->program.coordSourceMask |= 1u << static_cast<uint32_t>(layout->leafNodes.size());
  }
  if (HasShaderTiling(*parameters)) {
    layout->program.tiledLeaves.push_back(
        {static_cast<int>(layout->leafNodes.size()), *parameters->tiledRecipe});
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
  return true;
}

static bool EncodePerlinNoiseSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto* layout = ctx->layout;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
  if (parameters == nullptr || layout->noise != nullptr || inputBase != 0 ||
      parameters->permutationsView == nullptr || parameters->noiseView == nullptr) {
    return false;
  }
  layout->noise = parameters;
  slot.op = AOTChainOp::PerlinNoise;
  slot.noise = *parameters;
  return true;
}

static bool EncodeConstColorSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  slot.op = AOTChainOp::ConstColor;
  slot.constColor = *parameters;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

static bool EncodeUnaryPointwiseSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  if (node->inputs.size() != 1) {
    return false;
  }
  AOTPointwiseSlot pointwise = {};
  if (!BuildPointwiseSlot(*ctx->graph, node, &pointwise)) {
    return false;
  }
  // AOTPointwiseOpType and AOTChainOp share values 0..4 by ABI (both mirror the OP_*
  // constants), so the pointwise kinds convert directly.
  slot.op = static_cast<AOTChainOp>(static_cast<int>(pointwise.type));
  slot.colorMatrix = pointwise.colorMatrix;
  slot.luma = pointwise.luma;
  slot.alphaThreshold = pointwise.alphaThreshold;
  slot.colorSpaceXform = pointwise.colorSpaceXform;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

static bool EncodeInputOpaqueSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  if (node->inputs.size() != 1) {
    return false;
  }
  slot.op = AOTChainOp::InputOpaque;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

static bool EncodeMulAlphaSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  if (node->inputs.size() != 2) {
    return false;
  }
  slot.op = AOTChainOp::MulAlpha;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  slot.in1 = MapChainOpInput(ctx, inputBase, node->inputs[1]);
  return true;
}

static bool EncodeBlendSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTBlendParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 2) {
    return false;
  }
  if (parameters->childType == 2 && inputBase != 0) {
    // Two-child blends lower their children with the opaque-alpha geometry input, which
    // only the color graph carries.
    return false;
  }
  slot.op = AOTChainOp::Blend;
  slot.blend = *parameters;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  slot.in1 = MapChainOpInput(ctx, inputBase, node->inputs[1]);
  return true;
}

static bool EncodeRectCoverageSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  if (IsIdentityMatrix(parameters->deviceToLocal)) {
    // The kernel carries two CoverageRect array elements per space (the largest clip stack
    // either space produces in practice); a third one keeps the runtime route.
    if (ctx->rectCoverageSlots >= 2) {
      return false;
    }
    ++ctx->rectCoverageSlots;
    slot.op = AOTChainOp::AARectCoverage;
    slot.rectCoverage = *parameters;
  } else {
    // Same for the local-space form through the CoverageLocalRect array.
    if (ctx->localRectCoverageSlots >= 2) {
      return false;
    }
    ++ctx->localRectCoverageSlots;
    slot.op = AOTChainOp::LocalRectCoverage;
    slot.localRectCoverage = *parameters;
  }
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

static bool EncodeRRectCoverageSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  // The kernel carries four CoverageRRect* array elements; ValidateSlots rejects a fifth
  // rrect slot.
  slot.op = AOTChainOp::RRectCoverage;
  slot.rrectCoverage = *parameters;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

static bool EncodeGradientSlot(ChainBuildContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto* layout = ctx->layout;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTGradientParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  // Single-authority budget: AOTPointwiseChainProcessor::MaxGradientSlots (the kernel
  // carries one chain-wide gradient parameter block and one SourceCoords varying).
  if (ctx->hasGradient) {
    return false;
  }
  ctx->hasGradient = true;
  if (parameters->colorizerKind == 3) {
    // A LUT gradient binds its baked texture as a sampler-only child after the DAG leaves.
    if (parameters->lutProxy == nullptr) {
      return false;
    }
    layout->lutGradient = parameters;
  }
  slot.op = AOTChainOp::Gradient;
  slot.gradient = *parameters;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

static constexpr ChainNodeEncoder ChainEncoders[] = {
    nullptr,                   // GeometryColor (graph root, never scheduled)
    EncodeTextureSourceSlot,   // TextureSource
    EncodeUnaryPointwiseSlot,  // ColorMatrix
    EncodeUnaryPointwiseSlot,  // Luma
    EncodeUnaryPointwiseSlot,  // AlphaThreshold
    EncodeUnaryPointwiseSlot,  // ColorSpaceXform
    EncodeConstColorSlot,      // ConstColor
    EncodeBlendSlot,           // Blend
    EncodePerlinNoiseSlot,     // PerlinNoiseSource
    EncodeRectCoverageSlot,    // RectCoverage
    EncodeGradientSlot,        // GradientSource
    EncodeRRectCoverageSlot,   // RRectCoverage
    nullptr,                   // GeometryCoverage (graph root, never scheduled)
    nullptr,                   // GeometryColorOpaqueInput (designator, never scheduled)
    nullptr,                   // GeometryWhiteInput (designator, never scheduled)
    EncodeInputOpaqueSlot,     // InputOpaque
    EncodeMulAlphaSlot,        // MulAlpha
};
static_assert(sizeof(ChainEncoders) / sizeof(ChainEncoders[0]) == 17,
              "every AOTEffectKind needs one encoder row");

// Stage four's per-kind resource demand: a kind that consumes kernel resources adds its demand,
// every other kind adds none. The noise demand is counted from the layout's noise record instead
// (the layout owns it after stage three), so its row is null.
using ChainNodeDemandFn = void (*)(const AOTEffectNode* node, AOTResourceDemand* demand);

static void DemandTextureSource(const AOTEffectNode* node, AOTResourceDemand* demand) {
  auto* texture = std::get_if<AOTTextureParameters>(&node->parameters);
  if (texture == nullptr) {
    return;
  }
  ++demand->leafBindings;
  if (HasShaderTiling(*texture)) {
    ++demand->tiledRecipes;
  }
}

static void DemandGradientSource(const AOTEffectNode* node, AOTResourceDemand* demand) {
  auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters);
  if (gradient == nullptr) {
    return;
  }
  ++demand->gradients;
  if (gradient->colorizerKind == 3) {
    ++demand->leafBindings;
  }
}

static constexpr ChainNodeDemandFn ChainDemands[] = {
    nullptr,               // GeometryColor
    DemandTextureSource,   // TextureSource
    nullptr,               // ColorMatrix
    nullptr,               // Luma
    nullptr,               // AlphaThreshold
    nullptr,               // ColorSpaceXform
    nullptr,               // ConstColor
    nullptr,               // Blend
    nullptr,               // PerlinNoiseSource (counted from the layout's noise record)
    nullptr,               // RectCoverage
    DemandGradientSource,  // GradientSource
    nullptr,               // RRectCoverage
    nullptr,               // GeometryCoverage
    nullptr,               // GeometryColorOpaqueInput
    nullptr,               // GeometryWhiteInput
    nullptr,               // InputOpaque
    nullptr,               // MulAlpha
};
static_assert(sizeof(ChainDemands) / sizeof(ChainDemands[0]) == 17,
              "every AOTEffectKind needs one demand row");

// Stage three: encodes every scheduled node into its chain slot (the per-kind op, its parameter
// block, and its mapped input registers), collecting the texture leaves, the LUT gradient and the
// noise source into the layout along the way. Per-kind budgets that need no cross-node state are
// enforced by the encoders (the rect/rrect coverage capacities, the single gradient).
static bool EncodeChainSlots(ChainBuildContext* ctx) {
  auto* layout = ctx->layout;
  ctx->slots.assign(ctx->ordered.size(), AOTChainSlot{});
  ctx->rectCoverageSlots = 0;
  ctx->localRectCoverageSlots = 0;
  ctx->hasGradient = false;
  // All-ones reproduces the legacy behavior (every target reads the uvCoord attribute when the GP
  // carries one). The atlas-text route clears it and sets only the coverage leaves' bits.
  layout->program.coordSourceMask = ctx->coverageLeafFromUVCoord ? 0u : ~0u;
  layout->leafNodes.clear();
  layout->program.tiledLeaves.clear();
  layout->lutGradient = nullptr;
  layout->noise = nullptr;
  layout->deviceLeafIndices.clear();
  for (size_t index = 0; index < ctx->ordered.size(); ++index) {
    const size_t combined = ctx->ordered[index];
    auto node = ctx->nodes[combined];
    const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
    for (auto input : node->inputs) {
      if (inputBase + input.index() >=
          (combined < ctx->colorCount ? ctx->colorCount : ctx->nodes.size())) {
        return false;
      }
    }
    auto encoder = ChainEncoders[static_cast<size_t>(node->kind)];
    if (encoder == nullptr || !encoder(ctx, combined, index)) {
      return false;
    }
    auto& slot = ctx->slots[index];
    if (slot.op != AOTChainOp::Texture && slot.op != AOTChainOp::PerlinNoise &&
        (slot.in0 == -2 ||
         ((slot.op == AOTChainOp::Blend || slot.op == AOTChainOp::MulAlpha) && slot.in1 == -2))) {
      // An input edge whose producer is outside the pass: the plan omitted a node it depends on.
      return false;
    }
  }
  return true;
}

// Stage four: the pass's resource demand picks the final profile, the family's cross-slot
// constraints close over it, and the pass output's slot becomes the chain's root.
static bool ResolveFinalChainProfile(ChainBuildContext* ctx) {
  auto* layout = ctx->layout;
  // The pass kernel and its declared profile describe the color pass alone; the draw's coverage
  // may only raise the final profile (a coverage gradient or a third tiled leaf moves a Chain pass
  // onto the TiledGraph family, whose budgets are a superset for those fields).
  AOTResourceDemand colorDemand = {};
  for (auto nodeID : ctx->pass->nodes) {
    const auto* node = ctx->nodes[nodeID.index()];
    auto demand = ChainDemands[static_cast<size_t>(node->kind)];
    if (demand != nullptr) {
      demand(node, &colorDemand);
    }
  }
  colorDemand.noiseSources = layout->noise != nullptr ? 1 : 0;
  colorDemand.leafBindings += colorDemand.noiseSources * 2;
  auto declaredProfile = ctx->pass->resourceProfile;
  if (declaredProfile == AOTResourceProfile::Automatic) {
    declaredProfile = SelectAOTResourceProfile(colorDemand);
  }
  if (!FitsAOTResourceProfile(colorDemand, declaredProfile) ||
      (ctx->checkKernel && IsAOTGraphProfile(declaredProfile) !=
                               (ctx->pass->kernel == AOTKernelKind::PointwiseGraph))) {
    return false;
  }
  ctx->finalProfile = declaredProfile;
  if (ctx->finalProfile == AOTResourceProfile::Chain &&
      (ctx->hasGradient ||
       layout->program.tiledLeaves.size() > ChainResourceCapacity.tiledRecipes)) {
    if (ctx->coverageGraph == nullptr) {
      return false;
    }
    ctx->finalProfile = AOTResourceProfile::TiledGraph;
  }
  const size_t leafCount = layout->leafNodes.size();
  if ((layout->lutGradient != nullptr && leafCount >= 2) ||
      (layout->noise != nullptr &&
       (ctx->hasGradient || leafCount > 2 || ctx->coverageLeafFromUVCoord))) {
    return false;
  }
  ctx->rootIndex = ctx->slotOf[ctx->pass->output.index()];
  return ctx->rootIndex != SIZE_MAX;
}

// Stage five: an early-fold clip arrived as a RectCoverage/RRectCoverage trailing suffix
// multiplying the color root. Geometric coverage must ride the XP's coverage input instead of the
// source, so this pops the suffix and rewires it like the narrow clip slots: the innermost link
// chains from the scalar unit one, outer links chain upward, the color root moves to the node
// under the suffix, and the outermost link's register becomes the clip-coverage value. The suffix
// stays in the color chain when the node under it is a designator with no slot (e.g. a bare clip
// over the geometry color) — that shape keeps the old source-fold form. Walk in NODE-ordinal
// space (slotOf maps nodes to slots).
static void RewriteClipSuffix(ChainBuildContext* ctx) {
  std::vector<size_t> suffixSlots = {};
  size_t walkNode = ctx->pass->output.index();
  while (walkNode < ctx->colorCount &&
         (ctx->nodes[walkNode]->kind == AOTEffectKind::RectCoverage ||
          ctx->nodes[walkNode]->kind == AOTEffectKind::RRectCoverage)) {
    if (ctx->nodes[walkNode]->inputs.empty()) {
      break;
    }
    suffixSlots.push_back(ctx->slotOf[walkNode]);
    walkNode = ctx->nodes[walkNode]->inputs[0].index();
  }
  if (suffixSlots.empty() || walkNode >= ctx->colorCount || ctx->slotOf[walkNode] == SIZE_MAX) {
    return;
  }
  ctx->rootIndex = ctx->slotOf[walkNode];
  for (size_t i = 0; i < suffixSlots.size(); ++i) {
    ctx->slots[suffixSlots[i]].in0 =
        i + 1 < suffixSlots.size() ? static_cast<int>(suffixSlots[i + 1]) : -4;
  }
  ctx->clipCoverageSlot = static_cast<int>(suffixSlots.front());
  ctx->clipChainInput = ctx->clipCoverageSlot;
}

// Stage six: resolves the coverage subtree's root register, and validates the subtree's shape.
// Accepted roots: a single blend consuming the unit input (the xfer-dst coverage family), a bare
// texture or gradient consuming the unit alpha, a constant (the inverted shader mask filter whose
// shader yields no processor: ConstColor in any input mode, the same pmApplyConstColor math as
// the runtime emission), or an analytic coverage node (RectCoverage / RRectCoverage) closing a
// chain that starts at the unit. The unit may otherwise feed only texture leaves (their input is
// unused — blend operands sample raw), the gradient (a blend child, so it reads the opaque -4
// designator), and analytic coverage nodes. Analytic clip slots may multiply a blend-rooted
// subtree: the scalar coverage commutes with the pointwise blend result, so the product matches
// the runtime chain order (verified differentially). Two-child blends keep the plain route rather
// than risking a wrong value when the GP coverage is below 1.0.
static bool ResolveCoverageRoot(ChainBuildContext* ctx) {
  // A coverage graph without a valid root carries only analytic clip links: no coverage subtree
  // exists (the links ride the clip channel), so there is nothing to resolve here.
  if (ctx->coverageGraph == nullptr || !ctx->coverageRoot.isValid()) {
    return true;
  }
  const size_t covRootCombined = ctx->colorCount + ctx->coverageRoot.index();
  auto* covRootNode = ctx->nodes[covRootCombined];
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
  for (size_t j = 1; j < ctx->covCount; ++j) {
    auto* node = ctx->nodes[ctx->colorCount + j];
    if (node->kind == AOTEffectKind::Blend) {
      ++coverageBlendCount;
    }
    for (auto input : node->inputs) {
      if (input.index() != 0) {
        continue;
      }
      if (ctx->colorCount + j == covRootCombined) {
        rootConsumesUnit = true;
        continue;
      }
      if (node->kind != AOTEffectKind::TextureSource &&
          node->kind != AOTEffectKind::GradientSource &&
          node->kind != AOTEffectKind::RectCoverage && node->kind != AOTEffectKind::RRectCoverage) {
        return false;
      }
    }
  }
  // An analytic root consumes the previous analytic node rather than the unit, but the chain
  // still transmits the unit coverage transitively, so only analytic roots may omit a direct
  // unit input (the blend-count rule already rejects blends under an analytic root).
  if (coverageBlendCount != (rootIsBlend ? 1 : 0) || (!rootConsumesUnit && !rootIsAnalytic)) {
    return false;
  }
  auto covSlot = ctx->slotOf[covRootCombined];
  if (covSlot == SIZE_MAX) {
    return false;
  }
  ctx->coverageRootSlot = static_cast<int>(covSlot);
  if (!ctx->coverageLeafFromUVCoord) {
    return true;
  }
  // The atlas glyph mask is TRUE coverage in the runtime composite (the dedicated MaskFill
  // shader carries it as coverage — see AtlasTextNonSrcOverBlendKeepsDstProbe), unlike a
  // MaskFilter's source-modulating mask, whose subtree keeps the CoverageRootIndex source
  // route. Route the atlas subtree's root through the clip-coverage register so it rides the
  // XP's coverage input with the unpremultiplied source. A draw clip then chains on top of
  // the glyph (glyph * clip), so the glyph register is the clip chain's innermost input. An
  // early-fold clip suffix sits in the color block, ahead of the glyph slots, and cannot
  // read the glyph register.
  if (ctx->clipCoverageSlot >= 0) {
    return false;
  }
  ctx->clipCoverageSlot = ctx->coverageRootSlot;
  ctx->clipChainInput = ctx->coverageRootSlot;
  ctx->coverageRootSlot = -1;
  return true;
}

// Stage seven: routes the analytic coverage terms' links onto the clip-coverage channel. They
// never multiply the color root: each link chains from ctx->clipChainInput (the scalar unit one,
// or the glyph register the atlas path set) so their product is a pure clip coverage value the
// kernel reads through ClipCoverageRegister and composites as the XP's coverage input on top of
// the GP coverage — folding them into the source was only correct for SrcOver-class blending, and
// chaining them from the coverage unit (-3) squared the GP coverage. The links were encoded as
// ordinary slots by stage three; this only rewires their inputs, in chain order, and the color
// root stays the pre-clip result.
static void RouteAnalyticChains(ChainBuildContext* ctx) {
  if (ctx->clipLinks == nullptr) {
    return;
  }
  for (auto link : *ctx->clipLinks) {
    auto slot = ctx->slotOf[ctx->colorCount + link.index()];
    if (slot == SIZE_MAX) {
      continue;
    }
    ctx->slots[slot].in0 = ctx->clipChainInput;
    ctx->clipChainInput = static_cast<int>(slot);
    ctx->clipCoverageSlot = static_cast<int>(slot);
  }
}

// Stage eight: register allocation. The slot wiring built above uses instruction ordinals; this
// pass rewrites every reference into the kernel's recycled chainResults registers, so the
// per-profile instruction budget (16 for Chain and gradient, 32 for noise) and the live-result
// budget (MaxRegisters) are independent. A DAG exceeding the register budget is refused here.
static bool AssignChainRegisters(ChainBuildContext* ctx) {
  if (ctx->slots.size() > AOTPointwiseChainProcessor::MaxSlots) {
    return false;
  }
  std::vector<std::vector<int>> instructionInputs(ctx->slots.size());
  for (size_t index = 0; index < ctx->slots.size(); ++index) {
    for (int input : {ctx->slots[index].in0, ctx->slots[index].in1}) {
      if (input >= 0) {
        instructionInputs[index].push_back(input);
      }
    }
  }
  ctx->assignment = {};
  if (!AllocateChainRegisters(instructionInputs, static_cast<int>(ctx->rootIndex),
                              ctx->coverageRootSlot, ctx->clipCoverageSlot,
                              AOTPointwiseChainProcessor::MaxRegisters, &ctx->assignment)) {
    return false;
  }
  for (size_t index = 0; index < ctx->slots.size(); ++index) {
    if (ctx->slots[index].in0 >= 0) {
      ctx->slots[index].in0 =
          ctx->assignment.outRegister[static_cast<size_t>(ctx->slots[index].in0)];
    }
    if (ctx->slots[index].in1 >= 0) {
      ctx->slots[index].in1 =
          ctx->assignment.outRegister[static_cast<size_t>(ctx->slots[index].in1)];
    }
    ctx->slots[index].outRegister = ctx->assignment.outRegister[index];
  }
  return true;
}

// Stage nine: the processor's own data rules and the final profile's capacities decide last, on
// the exact instruction array construction will hand to AOTPointwiseChainProcessor::Make.
static bool FinalizeChainLayout(ChainBuildContext* ctx) {
  auto* layout = ctx->layout;
  std::vector<int> tiledLeafIndices = {};
  for (const auto& tiled : layout->program.tiledLeaves) {
    tiledLeafIndices.push_back(tiled.leafIndex);
  }
  if (ctx->assignment.rootRegister < 0 ||
      !AOTPointwiseChainProcessor::ValidateSlots(
          ctx->slots, static_cast<size_t>(ctx->assignment.rootRegister),
          ctx->assignment.coverageRootRegister, ctx->assignment.clipCoverageRegister,
          layout->leafNodes.size(), layout->lutGradient != nullptr, tiledLeafIndices,
          &ctx->finalProfile)) {
    return false;
  }
  layout->program.slots = std::move(ctx->slots);
  layout->program.rootRegister = static_cast<size_t>(ctx->assignment.rootRegister);
  layout->program.coverageRootRegister = ctx->assignment.coverageRootRegister;
  layout->program.clipCoverageRegister = ctx->assignment.clipCoverageRegister;
  layout->program.profile = ctx->finalProfile;
  return true;
}

// Flattens a single-pass pointwise DAG into the fused chain layout. Texture leaves are placed in
// the leading slots so that slot k pairs with TextureSampler_k, which keeps sampler indexing
// static in the kernel. When clipSlots carries analytic clip FPs, their coverage slots form the
// clip-coverage channel. When coverageGraph is given, its nodes (lowered from the coverage FP with
// the GeometryCoverage unit as origin) are merged after the color nodes: coverage leaves extend
// the leading texture block, coverage ops trail the color ops, and coverageRoot becomes the
// chain's coverage root. checkKernel compares the pass kernel against the profile the color pass
// needs; the DAG planner clears it to learn that profile.
static bool PlanChainLayout(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                            const AOTEffectGraph* coverageGraph, AOTNodeID coverageRoot,
                            bool coverageLeafFromUVCoord, const std::vector<AOTNodeID>& clipLinks,
                            bool checkKernel, AOTChainCompile* layout) {
  ChainBuildContext ctx = {};
  ctx.graph = &graph;
  ctx.pass = &pass;
  ctx.coverageGraph = coverageGraph;
  ctx.coverageRoot = coverageRoot;
  ctx.coverageLeafFromUVCoord = coverageLeafFromUVCoord;
  ctx.clipLinks = &clipLinks;
  ctx.checkKernel = checkKernel;
  ctx.layout = layout;
  if (!MergeNodeSpaces(&ctx)) {
    return false;
  }
  ScheduleChainSlots(&ctx);
  if (!EncodeChainSlots(&ctx) || !ResolveFinalChainProfile(&ctx)) {
    return false;
  }
  RewriteClipSuffix(&ctx);
  if (!ResolveCoverageRoot(&ctx)) {
    return false;
  }
  RouteAnalyticChains(&ctx);
  return AssignChainRegisters(&ctx) && FinalizeChainLayout(&ctx);
}

// The padding state that mirrors the first real leaf's sampler on the phantom children: on
// OpenGL the wrap/filter modes are texture-object state (not per-unit), so a default-state
// phantom bound after a real leaf would overwrite e.g. a decal clamp-to-border with
// clamp-to-edge for the whole draw.
struct ChainPaddingTexture {
  std::shared_ptr<TextureProxy> proxy = nullptr;
  SamplerState state = {};
};

static SamplingArgs PaddingSamplingArgs(const SamplerState& state) {
  SamplingOptions sampling(state.minFilterMode, state.magFilterMode, state.mipmapMode);
  return {state.tileModeX, state.tileModeY, sampling, SrcRectConstraint::Fast};
}

// One candidate for the padding texture: a resolved texture leaf (its tiled hardware sampler when
// it has one), or a LUT-gradient-only chain's baked LUT texture — a chain with no TextureSource
// node (no leaf binding exists there, so the LUT serves as the padding just as it served as the
// sampler child).
static void ConsiderPaddingNode(const AOTEffectNode* node, ChainPaddingTexture* padding) {
  if (padding->proxy != nullptr || node == nullptr) {
    return;
  }
  if (const auto* texture = std::get_if<AOTTextureParameters>(&node->parameters)) {
    padding->proxy = texture->textureProxy;
    if (texture->samplingKind == AOTTextureSamplingKind::Tiled &&
        texture->tiledRecipe.has_value()) {
      padding->state = texture->tiledRecipe->hardwareSampler;
    } else {
      padding->state = texture->samplerState;
    }
  } else if (const auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters)) {
    if (gradient->lutProxy != nullptr) {
      padding->proxy = gradient->lutProxy;
    }
  }
}

// Picks the padding texture by walking the color graph (only its in-pass nodes when the chain
// carries noise — the noise graph's whole node space is the family's, so out-of-pass color nodes
// must not contribute) and the coverage graph. Falls back to the noise tables' first proxy with
// nearest sampling.
static ChainPaddingTexture ChoosePaddingTexture(const AOTEffectGraph& graph,
                                                const AOTPassDescriptor& pass,
                                                const AOTEffectGraph* coverageGraph,
                                                const AOTPerlinNoiseParameters* noise,
                                                std::shared_ptr<TextureProxy> noisePaddingProxy) {
  ChainPaddingTexture padding = {};
  for (size_t index = 0; index < graph.nodeCount(); ++index) {
    AOTNodeID nodeID(static_cast<uint32_t>(index));
    if (noise != nullptr &&
        std::find(pass.nodes.begin(), pass.nodes.end(), nodeID) == pass.nodes.end()) {
      continue;
    }
    ConsiderPaddingNode(graph.nodeAt(nodeID), &padding);
  }
  for (size_t index = 0; coverageGraph != nullptr && index < coverageGraph->nodeCount(); ++index) {
    ConsiderPaddingNode(coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index))), &padding);
  }
  if (padding.proxy == nullptr && noisePaddingProxy != nullptr) {
    padding.proxy = std::move(noisePaddingProxy);
    padding.state = SamplerState(SamplingOptions(FilterMode::Nearest));
  }
  return padding;
}

// Binds the noise tables' two lookup textures as sampler-only children ahead of the phantom
// padding. They are real bindings the kernel reads through Nearest lookups; firstProxy reports
// the first one for the padding fallback. Returns false when a view cannot be wrapped.
static bool AppendNoiseLookupChildren(BlockAllocator* allocator,
                                      const AOTPerlinNoiseParameters* noise,
                                      std::vector<PlacementPtr<FragmentProcessor>>* samplerPadding,
                                      std::shared_ptr<TextureProxy>* firstProxy,
                                      std::vector<AOTBinding>* bindings) {
  for (const auto& view : {noise->permutationsView, noise->noiseView}) {
    if (view == nullptr || view->getContext() == nullptr) {
      return false;
    }
    auto proxy = view->getContext()->proxyProvider()->wrapTextureView(view);
    if (proxy == nullptr) {
      return false;
    }
    SamplingArgs lookupArgs = {TileMode::Clamp, TileMode::Clamp,
                               SamplingOptions(FilterMode::Nearest), SrcRectConstraint::Fast};
    auto matrix = Matrix::I();
    auto child = TextureEffect::Make(allocator, proxy, lookupArgs, &matrix);
    if (child == nullptr) {
      return false;
    }
    if (firstProxy != nullptr && *firstProxy == nullptr) {
      *firstProxy = proxy;
    }
    samplerPadding->push_back(std::move(child));
    bindings->push_back({AOTBinding::Kind::Padding, proxy, lookupArgs, matrix});
  }
  return true;
}

// Fills the sampler slots up to the four-leaf artifacts' TEXTURE_COUNT with phantom children
// re-using the padding texture. A chain that already binds four (or none) is left alone.
static bool PadSamplersToFour(BlockAllocator* allocator, const ChainPaddingTexture& padding,
                              std::vector<PlacementPtr<FragmentProcessor>>* samplerPadding,
                              size_t* samplerChildren, std::vector<AOTBinding>* bindings) {
  if (*samplerChildren == 0 || *samplerChildren >= 4) {
    return true;
  }
  if (padding.proxy == nullptr) {
    return false;
  }
  auto paddingArgs = PaddingSamplingArgs(padding.state);
  while (*samplerChildren < 4) {
    auto phantomMatrix = Matrix::I();
    auto phantom = TextureEffect::Make(allocator, padding.proxy, paddingArgs, &phantomMatrix);
    if (phantom == nullptr) {
      return false;
    }
    samplerPadding->push_back(std::move(phantom));
    bindings->push_back({AOTBinding::Kind::Padding, padding.proxy, paddingArgs, phantomMatrix});
    ++*samplerChildren;
  }
  return true;
}

// The mask child: the real device-space mask, or — for a mask-free chain that bound all four
// sampler slots — a phantom there, which the kernel identifies by HasMaskTexture and skips.
struct ChainMaskChild {
  PlacementPtr<FragmentProcessor> child = nullptr;
  bool isPhantom = false;
};

static bool BuildChainMaskChild(BlockAllocator* allocator, const AOTTextureParameters* mask,
                                const ChainPaddingTexture& padding, size_t samplerChildren,
                                ChainMaskChild* result, std::vector<AOTBinding>* bindings) {
  if (mask != nullptr) {
    // The mask child's Subset write routes to the kernel's DeviceMaskSubset decoy, so a leaf's
    // real subset rect survives in the shared uniform block.
    result->child = DeviceSpaceTextureEffect::Make(allocator, mask->textureProxy, mask->uvMatrix);
    if (result->child == nullptr) {
      return false;
    }
    bindings->push_back({AOTBinding::Kind::Mask, mask->textureProxy, {}, mask->uvMatrix});
    return true;
  }
  if (samplerChildren != 4) {
    return true;
  }
  if (padding.proxy == nullptr) {
    return false;
  }
  auto paddingArgs = PaddingSamplingArgs(padding.state);
  auto phantomMatrix = Matrix::I();
  result->child = TextureEffect::Make(allocator, padding.proxy, paddingArgs, &phantomMatrix);
  result->isPhantom = result->child != nullptr;
  if (result->child != nullptr) {
    bindings->push_back({AOTBinding::Kind::Padding, padding.proxy, paddingArgs, phantomMatrix});
  }
  return result->child != nullptr;
}

// Merges the draw's coverage terms into ONE coverage graph for the layout pipeline: one
// geometry-coverage unit, the analytic terms' links chained onto it (each term's innermost link
// continues the previous term's outermost — a pure product), and — when a subtree or glyph term
// exists — its nodes copied over with the term's own unit replaced by the combined one. The
// analytic links' node IDs are reported in chain order for stage seven's rewiring; the DeviceMask
// term's parameters are reported for the mask child. Returns false when the terms cannot combine
// (a subtree and a glyph together never lower — the atlas draw's single coverage slot is the
// glyph, so a subtree beside it has no route today).
struct ChainCoverageGraph {
  AOTEffectGraph graph = {};
  AOTNodeID coverageRoot = AOTNodeID::Invalid();
  std::vector<AOTNodeID> clipLinks = {};
  const AOTTextureParameters* mask = nullptr;
  bool coverageLeafFromUVCoord = false;
};

static bool MergeCoverageTerms(const std::vector<AOTCoverageTerm>& terms,
                               ChainCoverageGraph* merged) {
  AOTNodeBuilder builder = {};
  AOTNodeID unit = AOTNodeID::Invalid();
  if (!builder.addGeometryCoverage(&unit)) {
    return false;
  }
  AOTNodeID link = unit;
  for (const auto& term : terms) {
    if (term.kind == AOTCoverageTermKind::Analytic) {
      for (uint32_t index = 1; index < term.graph.nodeCount(); ++index) {
        auto* node = term.graph.nodeAt(AOTNodeID(index));
        AOTNodeID copied = AOTNodeID::Invalid();
        if (node == nullptr || !builder.addCopy(*node, {link}, node->parameters, &copied)) {
          return false;
        }
        link = copied;
        merged->clipLinks.push_back(copied);
      }
    } else if (term.kind == AOTCoverageTermKind::DeviceMask) {
      if (merged->mask != nullptr) {
        return false;
      }
      merged->mask = &term.mask;
    } else {
      if (merged->coverageRoot.isValid() || merged->coverageLeafFromUVCoord) {
        return false;
      }
      std::vector<AOTNodeID> mapped(term.graph.nodeCount(), AOTNodeID::Invalid());
      if (term.graph.nodeCount() == 0 || !term.root.isValid() ||
          term.root.index() >= term.graph.nodeCount()) {
        return false;
      }
      mapped[0] = unit;
      for (uint32_t index = 1; index < term.graph.nodeCount(); ++index) {
        auto* node = term.graph.nodeAt(AOTNodeID(index));
        if (node == nullptr) {
          return false;
        }
        std::vector<AOTNodeID> inputs = {};
        for (auto input : node->inputs) {
          if (input.index() >= mapped.size() || !mapped[input.index()].isValid()) {
            return false;
          }
          inputs.push_back(mapped[input.index()]);
        }
        AOTNodeID copied = AOTNodeID::Invalid();
        if (!builder.addCopy(*node, inputs, node->parameters, &copied)) {
          return false;
        }
        mapped[index] = copied;
      }
      merged->coverageRoot = mapped[term.root.index()];
      merged->coverageLeafFromUVCoord = term.kind == AOTCoverageTermKind::Glyph;
    }
  }
  auto root = merged->coverageRoot.isValid() ? merged->coverageRoot : link;
  return builder.finish(root, &merged->graph);
}

// Rebuilds the fused chain processor from a planned layout: the texture leaves, the LUT and noise
// sampler-only children, the phantom padding the four-sampler artifacts need and the mask child.
// Every slot and budget decision was already taken by PlanChainLayout; a failure here can only
// come from a resource that cannot be wrapped into a processor. coverageTerms is null for a plan
// pass (plan passes carry no draw coverage).
static PlacementPtr<FragmentProcessor> BuildChainFP(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    const std::vector<AOTCoverageTerm>* coverageTerms) {
  ChainCoverageGraph coverage = {};
  const AOTEffectGraph* coverageGraph = nullptr;
  if (coverageTerms != nullptr && !coverageTerms->empty()) {
    if (!MergeCoverageTerms(*coverageTerms, &coverage)) {
      return nullptr;
    }
    coverageGraph = &coverage.graph;
  }
  AOTChainCompile layout = {};
  if (!PlanChainLayout(graph, pass, coverageGraph, coverage.coverageRoot,
                       coverage.coverageLeafFromUVCoord, coverage.clipLinks, true, &layout)) {
    return nullptr;
  }
  AOTProgram program = std::move(layout.program);
  std::vector<AOTBinding> bindings = {};
  bindings.reserve(layout.leafNodes.size() + 5);
  std::vector<PlacementPtr<FragmentProcessor>> leaves = {};
  leaves.reserve(layout.leafNodes.size());
  for (auto* node : layout.leafNodes) {
    auto leaf = BuildTextureLeafChild(allocator, node);
    if (leaf.processor == nullptr || !IsProcessor<TextureEffect>(leaf.processor.get())) {
      return nullptr;
    }
    leaves.push_back(std::move(leaf.processor));
    bindings.push_back(std::move(leaf.binding));
  }
  // A LUT gradient carries its baked texture as a sampler-only child appended after the DAG
  // leaves; the OP_GRADIENT LUT branch reads it through the GradientLUTLeaf uniform.
  PlacementPtr<FragmentProcessor> lutChild = nullptr;
  program.lutLeafIndex = -1;
  if (layout.lutGradient != nullptr) {
    SamplingOptions lutSampling(FilterMode::Linear, MipmapMode::None);
    SamplingArgs lutArgs = {TileMode::Clamp, TileMode::Clamp, lutSampling, SrcRectConstraint::Fast};
    auto lutMatrix = Matrix::I();
    lutChild = TextureEffect::Make(allocator, layout.lutGradient->lutProxy, lutArgs, &lutMatrix);
    if (lutChild == nullptr) {
      return nullptr;
    }
    program.lutLeafIndex = static_cast<int>(leaves.size());
    bindings.push_back({AOTBinding::Kind::Lut, layout.lutGradient->lutProxy, lutArgs, lutMatrix});
  }
  // TEXTURE_COUNT exists only as 0 or 4: chains with 1-3 sampler children ride the four-leaf
  // artifacts by binding phantom children that re-use the first leaf's texture. Their DAG slots
  // are never OP_TEXTURE, so the kernel's runtime guard never samples them. The phantom must
  // mirror the first leaf's sampler state: on OpenGL the wrap/filter modes are texture-object
  // state (not per-unit), so a default-state phantom bound after the real leaf would overwrite
  // e.g. a decal clamp-to-border with clamp-to-edge for the whole draw.
  std::vector<PlacementPtr<FragmentProcessor>> samplerPadding = {};
  std::shared_ptr<TextureProxy> noisePaddingProxy = nullptr;
  if (layout.noise != nullptr &&
      !AppendNoiseLookupChildren(allocator, layout.noise, &samplerPadding, &noisePaddingProxy,
                                 &bindings)) {
    return nullptr;
  }
  auto padding =
      ChoosePaddingTexture(graph, pass, coverageGraph, layout.noise, std::move(noisePaddingProxy));
  size_t samplerChildren = leaves.size() + (lutChild != nullptr ? 1 : 0) + samplerPadding.size();
  if (!PadSamplersToFour(allocator, padding, &samplerPadding, &samplerChildren, &bindings)) {
    return nullptr;
  }
  ChainMaskChild maskChildResult = {};
  if (!BuildChainMaskChild(allocator, coverage.mask, padding, samplerChildren, &maskChildResult,
                           &bindings)) {
    return nullptr;
  }
  program.hasMaskTexture = maskChildResult.child != nullptr && !maskChildResult.isPhantom;
  program.noiseSamplerBase = layout.noise != nullptr ? static_cast<int>(leaves.size()) : -1;
  program.deviceLeaves.reserve(layout.deviceLeafIndices.size());
  for (auto leafIndex : layout.deviceLeafIndices) {
    const auto* parameters = std::get_if<AOTTextureParameters>(
        &layout.leafNodes[static_cast<size_t>(leafIndex)]->parameters);
    if (parameters == nullptr) {
      return nullptr;
    }
    program.deviceLeaves.push_back({leafIndex, parameters->textureProxy, parameters->uvMatrix});
  }
  program.bindings = std::move(bindings);
  AOTPointwiseChainProcessor::AOTChainChildren children = {};
  children.leaves = std::move(leaves);
  children.lut = std::move(lutChild);
  children.padding = std::move(samplerPadding);
  children.mask = std::move(maskChildResult.child);
  children.maskIsPhantom = maskChildResult.isPhantom;
  return AOTPointwiseChainProcessor::Make(allocator, std::move(program), std::move(children));
}

}  // namespace

bool AOTChainBuilder::PlanChainPass(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                                    bool checkKernel, AOTResourceProfile* outProfile) {
  AOTChainCompile compile = {};
  if (!Compile(graph, pass, nullptr, checkKernel, &compile)) {
    return false;
  }
  if (outProfile != nullptr) {
    *outProfile = compile.program.profile;
  }
  return true;
}

bool AOTChainBuilder::Compile(const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
                              const std::vector<AOTCoverageTerm>* coverageTerms, bool checkKernel,
                              AOTChainCompile* out) {
  if (out == nullptr) {
    return false;
  }
  ChainCoverageGraph coverage = {};
  const AOTEffectGraph* coverageGraph = nullptr;
  if (coverageTerms != nullptr && !coverageTerms->empty()) {
    if (!MergeCoverageTerms(*coverageTerms, &coverage)) {
      return false;
    }
    coverageGraph = &coverage.graph;
  }
  return PlanChainLayout(graph, pass, coverageGraph, coverage.coverageRoot,
                         coverage.coverageLeafFromUVCoord, coverage.clipLinks, checkKernel, out);
}

AOTTextureParameters AOTChainBuilder::IntermediateSource(std::shared_ptr<TextureProxy> textureProxy,
                                                         const Matrix& uvMatrix) {
  AOTTextureParameters parameters = {};
  parameters.textureProxy = std::move(textureProxy);
  parameters.samplingKind = AOTTextureSamplingKind::Device;
  parameters.uvMatrix = uvMatrix;
  return parameters;
}

// Maps one original-graph input edge onto the segment copy: an edge pointing at a cut root
// resolves to that cut's replacement leaf; the geometry color is every segment's own node 0 (a
// source node chained onto it in the original graph keeps that placeholder input in the copy); any
// other edge looks its node up in the copy.
static AOTNodeID ResolveCopiedInput(const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                                    const std::vector<AOTNodeID>& cutRoots,
                                    const std::vector<AOTNodeID>& cutLeaves, AOTNodeID input) {
  for (size_t index = 0; index < cutRoots.size(); ++index) {
    if (input == cutRoots[index]) {
      return cutLeaves[index];
    }
  }
  if (input == AOTNodeID(0)) {
    return AOTNodeID(0);
  }
  auto found = builtMap.find(input.index());
  return found == builtMap.end() ? AOTNodeID::Invalid() : found->second;
}

// Precomposes the coordinate matrices a copied node's parameters carry: a plain or tiled texture
// leaf's uvMatrix, a gradient's coordMatrix and a noise source's uvMatrix with coordToLocal, while
// a device-sampled texture leaf's uvMatrix is precomposed with deviceLeafRemap instead (it
// addresses the destination's positions). The device-space coverage parameters stay as they are
// (the fill's positions are the device ones). Every other parameter block carries no coordinates
// and passes through unchanged. A terminal pass passes identity for both matrices, because it runs
// on the destination and sees the original spaces.
static AOTEffectParameters RemapNodeParameters(AOTEffectParameters parameters,
                                               const Matrix& coordToLocal,
                                               const Matrix& deviceLeafRemap) {
  if (auto* texture = std::get_if<AOTTextureParameters>(&parameters)) {
    if (texture->samplingKind == AOTTextureSamplingKind::Device) {
      texture->uvMatrix.preConcat(deviceLeafRemap);
    } else {
      texture->uvMatrix.preConcat(coordToLocal);
    }
  } else if (auto* gradient = std::get_if<AOTGradientParameters>(&parameters)) {
    gradient->coordMatrix.preConcat(coordToLocal);
  } else if (auto* noise = std::get_if<AOTPerlinNoiseParameters>(&parameters)) {
    noise->uvMatrix.preConcat(coordToLocal);
  }
  return parameters;
}

// Copies one chain node into the builder, resolving its input edges through builtMap (and the cut
// replacements). The source node was admitted by its own lowering when the graph was built, so the
// copy needs no kind-specific admission — only the coordinate remap above.
static bool CopyChainNode(const AOTEffectGraph& graph, AOTNodeID nodeID,
                          const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                          const std::vector<AOTNodeID>& cutRoots,
                          const std::vector<AOTNodeID>& cutLeaves, const Matrix& coordToLocal,
                          const Matrix& deviceLeafRemap, AOTNodeBuilder* builder,
                          AOTNodeID* copied) {
  const auto* node = graph.nodeAt(nodeID);
  if (node == nullptr || builder == nullptr || copied == nullptr) {
    return false;
  }
  std::vector<AOTNodeID> inputs = {};
  inputs.reserve(node->inputs.size());
  for (auto input : node->inputs) {
    auto resolved = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, input);
    if (!resolved.isValid()) {
      return false;
    }
    inputs.push_back(resolved);
  }
  return builder->addCopy(
      *node, inputs, RemapNodeParameters(node->parameters, coordToLocal, deviceLeafRemap), copied);
}

// Copies the wanted node set into the builder in original-graph order — the graph is SSA, so every
// input precedes its consumer and each copy's inputs are always already built. Designators are
// always copied (a pass shares them with its cut subtrees), nodes in skip never are. The first
// consumer referencing a cut root builds that cut's replacement texture leaf right before itself
// (fed from the opaque-white designator so the intermediate samples raw: the producing pass
// already applied its own modulation, and a geometry-color input would fold the paint alpha in a
// second time).
static bool CopyChainNodes(const AOTEffectGraph& graph, const std::vector<bool>& wanted,
                           const std::vector<bool>& skip, const std::vector<AOTNodeID>& cutRoots,
                           const std::vector<AOTTextureParameters>& intermediates,
                           const Matrix& coordToLocal, const Matrix& deviceLeafRemap,
                           AOTNodeBuilder* builder,
                           std::unordered_map<uint32_t, AOTNodeID>* builtMap) {
  if (cutRoots.size() != intermediates.size()) {
    return false;
  }
  std::vector<AOTNodeID> cutLeaves(cutRoots.size(), AOTNodeID::Invalid());
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto* node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return false;
    }
    bool isDesignator = TraitsOf(node->kind).inputDesignator;
    if ((!wanted[index] && !isDesignator) || (skip[index] && !isDesignator)) {
      continue;
    }
    for (size_t cut = 0; cut < cutRoots.size(); ++cut) {
      if (cutLeaves[cut].isValid()) {
        continue;
      }
      bool references = false;
      for (auto input : node->inputs) {
        references |= input == cutRoots[cut];
      }
      if (!references) {
        continue;
      }
      AOTNodeID white = AOTNodeID::Invalid();
      if (!builder->addGeometryWhiteInput(&white) ||
          !builder->addTextureSource(white, intermediates[cut], &cutLeaves[cut])) {
        return false;
      }
    }
    AOTNodeID copied = AOTNodeID::Invalid();
    if (!CopyChainNode(graph, AOTNodeID(index), *builtMap, cutRoots, cutLeaves, coordToLocal,
                       deviceLeafRemap, builder, &copied)) {
      return false;
    }
    (*builtMap)[index] = copied;
  }
  return true;
}

// Finishes the builder's graph at the mapped root and resolves the segment pass over the copy:
// the node list in the caller's order, the profile and the kernel the segment's own graph settles
// on — which is the pass's real fit.
static bool FinishSegmentPass(const AOTNodeBuilder& builder, AOTNodeID root,
                              const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                              AOTEffectGraph* segment, AOTPassDescriptor* pass) {
  auto rootMapped = ResolveCopiedInput(builtMap, {}, {}, root);
  if (!rootMapped.isValid()) {
    return false;
  }
  AOTEffectGraph built = {};
  if (!builder.finish(rootMapped, &built)) {
    return false;
  }
  AOTPassDescriptor resultPass = {};
  resultPass.output = rootMapped;
  // The pass node list is every non-designator node of the segment graph itself — the copy may
  // have added nodes the caller did not list (the replacement texture leaf), so every
  // non-designator node of the segment graph itself is listed.
  for (uint32_t index = 1; index < built.nodeCount(); ++index) {
    auto* node = built.nodeAt(AOTNodeID(index));
    if (node == nullptr || node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
        node->kind == AOTEffectKind::GeometryWhiteInput) {
      continue;
    }
    resultPass.nodes.push_back(AOTNodeID(index));
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!AOTChainBuilder::PlanChainPass(built, resultPass, false, &profile)) {
    return false;
  }
  resultPass.resourceProfile = profile;
  resultPass.kernel =
      IsAOTGraphProfile(profile) ? AOTKernelKind::PointwiseGraph : AOTKernelKind::PointwiseChain;
  *segment = std::move(built);
  *pass = std::move(resultPass);
  return true;
}

bool AOTChainBuilder::BuildCutSegment(const AOTEffectGraph& graph, const AOTPassDescriptor& cutPass,
                                      const std::vector<AOTTextureParameters>& intermediates,
                                      const Matrix& coordToLocal, const Matrix& deviceLeafRemap,
                                      AOTEffectGraph* segment, AOTPassDescriptor* pass) {
  if (segment == nullptr || pass == nullptr || cutPass.nodes.empty() ||
      cutPass.materializedInputs.size() != intermediates.size()) {
    return false;
  }
  std::vector<bool> skip(graph.nodeCount(), false);
  for (auto cut : cutPass.materializedInputs) {
    if (!cut.isValid() || cut.index() >= graph.nodeCount()) {
      return false;
    }
    std::vector<bool> cutReachable(graph.nodeCount(), false);
    if (!CollectReachableNodes(graph, cut, &cutReachable)) {
      return false;
    }
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      skip[index] = skip[index] || cutReachable[index];
    }
  }
  std::vector<bool> wanted(graph.nodeCount(), false);
  for (auto nodeID : cutPass.nodes) {
    if (!nodeID.isValid() || nodeID.index() >= graph.nodeCount() || skip[nodeID.index()]) {
      return false;
    }
    wanted[nodeID.index()] = true;
  }
  AOTNodeBuilder builder = {};
  AOTNodeID geometry = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&geometry)) {
    return false;
  }
  std::unordered_map<uint32_t, AOTNodeID> builtMap = {};
  if (!CopyChainNodes(graph, wanted, skip, cutPass.materializedInputs, intermediates, coordToLocal,
                      deviceLeafRemap, &builder, &builtMap)) {
    return false;
  }
  return FinishSegmentPass(builder, cutPass.output, builtMap, segment, pass);
}

PlacementPtr<FragmentProcessor> AOTChainBuilder::BuildFPForPass(BlockAllocator* allocator,
                                                                const AOTEffectGraph& graph,
                                                                const AOTPassDescriptor& pass) {
  if (pass.nodes.empty()) {
    return nullptr;
  }
  if (IsAOTPointwiseDAGKernel(pass.kernel)) {
    return BuildChainFP(allocator, graph, pass, nullptr);
  }
  // The linear kernels: the source node plus its operators folded into the processor's own slot
  // records, one program per pass with no Compose wrapper.
  std::vector<AOTPointwiseSlot> slots = {};
  if (!BuildLinearSlots(graph, pass, &slots)) {
    return nullptr;
  }
  auto sourceNode = graph.nodeAt(pass.nodes[0]);
  switch (pass.kernel) {
    case AOTKernelKind::YUVTextureFill: {
      auto source = BuildTextureLeafChild(allocator, sourceNode);
      if (source.processor == nullptr || !IsProcessor<TextureEffect>(source.processor.get())) {
        return nullptr;
      }
      return AOTYUVChainProcessor::Make(allocator, std::move(source.processor), slots);
    }
    case AOTKernelKind::PerlinNoiseFill: {
      auto parameters =
          sourceNode != nullptr && sourceNode->kind == AOTEffectKind::PerlinNoiseSource
              ? std::get_if<AOTPerlinNoiseParameters>(&sourceNode->parameters)
              : nullptr;
      if (parameters == nullptr || slots.size() > MaxPointwiseSlots) {
        return nullptr;
      }
      return PerlinNoiseFragmentProcessor::MakeFromAOTParameters(allocator, *parameters, slots);
    }
    default:
      return nullptr;
  }
}

PlacementPtr<FragmentProcessor> AOTChainBuilder::BuildChainProcessor(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    const std::vector<const FragmentProcessor*>& coverageFPs, bool coverageLeafFromUVCoord) {
  if (!IsAOTPointwiseDAGKernel(pass.kernel)) {
    return nullptr;
  }
  if (coverageFPs.empty()) {
    return BuildChainFP(allocator, graph, pass, nullptr);
  }
  if (coverageFPs.size() > 2) {
    return nullptr;
  }
  // The shared coverage classifier decides what each coverage leaf is (the same one LowerDraw
  // uses); coverageLeafFromUVCoord marks the trailing FP as the AtlasText glyph leaf a caller
  // synthesized from the geometry processor.
  const FragmentProcessor* glyphLeaf = coverageLeafFromUVCoord ? coverageFPs.back() : nullptr;
  auto terms = LowerCoverageTerms(coverageFPs, glyphLeaf, allocator);
  if (!terms.has_value()) {
    return nullptr;
  }
  return BuildChainFP(allocator, graph, pass, &*terms);
}

PlacementPtr<FragmentProcessor> AOTChainBuilder::BuildChainProcessorForInfo(
    BlockAllocator* allocator, const ProgramInfo* programInfo) {
  if (allocator == nullptr || programInfo == nullptr) {
    return nullptr;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp == nullptr) {
    return nullptr;
  }
  std::vector<const FragmentProcessor*> colorProcessors = {};
  colorProcessors.reserve(programInfo->numColorFragmentProcessors() + 1);
  for (size_t index = 0; index < programInfo->numColorFragmentProcessors(); ++index) {
    colorProcessors.push_back(programInfo->getFragmentProcessor(index));
  }
  std::vector<const FragmentProcessor*> coverageProcessors = {};
  for (size_t index = programInfo->numColorFragmentProcessors();
       index < programInfo->numFragmentProcessors(); ++index) {
    coverageProcessors.push_back(programInfo->getFragmentProcessor(index));
  }
  // One lowering for the whole draw: the color graph plus the classified coverage terms (the
  // classifier synthesizes the AtlasText glyph leaf from the geometry processor).
  auto ir = LowerDraw(colorProcessors, coverageProcessors, gp, programInfo->getXferProcessor(),
                      BlendMode::SrcOver, allocator);
  if (!ir.has_value()) {
    return nullptr;
  }
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Decompose(ir->color, &plan, true) || plan.passes.size() != 1 ||
      !IsAOTPointwiseDAGKernel(plan.passes[0].kernel)) {
    // The perlin kernels have their own families; the chain family only takes the DAG kernels,
    // and anything the single-pass decomposition cannot fit needs a plan task.
    return nullptr;
  }
  return BuildChainFP(allocator, ir->color, plan.passes[0], &ir->coverage);
}

PlacementPtr<FragmentProcessor> AOTChainBuilder::BuildPerlinProcessorForInfo(
    BlockAllocator* allocator, const ProgramInfo* programInfo) {
  if (allocator == nullptr || programInfo == nullptr) {
    return nullptr;
  }
  // The perlin kernel has no coverage-fold path: the perlin family only accepts a draw whose
  // processors are all color processors.
  if (programInfo->numFragmentProcessors() != programInfo->numColorFragmentProcessors()) {
    return nullptr;
  }
  std::vector<const FragmentProcessor*> colorProcessors = {};
  colorProcessors.reserve(programInfo->numColorFragmentProcessors());
  for (size_t index = 0; index < programInfo->numColorFragmentProcessors(); ++index) {
    colorProcessors.push_back(programInfo->getFragmentProcessor(index));
  }
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  if (!AOTRouter::PlanSinglePass(colorProcessors, &graph, &plan) ||
      plan.passes[0].kernel != AOTKernelKind::PerlinNoiseFill) {
    return nullptr;
  }
  return BuildFPForPass(allocator, graph, plan.passes[0]);
}

}  // namespace tgfx
