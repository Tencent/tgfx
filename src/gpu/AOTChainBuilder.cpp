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

// Rebuilds the texture processor of one TextureSource node: a plain or tiled leaf sampling through
// its resolved sampler state, or the stand-in leaf of a device-space source. Only a plain leaf may
// carry the RGBAAA layout: its rebuilt TextureEffect keeps the alpha-area offset and writes it to
// the kernel's per-leaf AlphaStart / HasRgbaaa fields.
static PlacementPtr<FragmentProcessor> BuildTextureLeafFP(BlockAllocator* allocator,
                                                          const AOTEffectNode* node) {
  auto parameters = node != nullptr && node->kind == AOTEffectKind::TextureSource
                        ? std::get_if<AOTTextureParameters>(&node->parameters)
                        : nullptr;
  if (parameters == nullptr ||
      (parameters->hasRGBAAA && parameters->samplingKind != AOTTextureSamplingKind::Plain)) {
    return nullptr;
  }
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
    return TextureEffect::Make(allocator, parameters->textureProxy, args, &uvMatrix);
  }
  if (parameters->samplingKind == AOTTextureSamplingKind::Tiled) {
    const auto& recipe = parameters->tiledRecipe;
    if (!recipe.has_value() || !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeX) ||
        !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeY)) {
      return nullptr;
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
    return TextureEffect::Make(allocator, parameters->textureProxy, args, &uvMatrix);
  }
  SamplingOptions sampling(parameters->samplerState.minFilterMode,
                           parameters->samplerState.magFilterMode,
                           parameters->samplerState.mipmapMode);
  SamplingArgs args = {parameters->samplerState.tileModeX, parameters->samplerState.tileModeY,
                       sampling, parameters->constraint};
  args.sampleArea = parameters->subset;
  auto uvMatrix = parameters->uvMatrix;
  return TextureEffect::MakeRGBAAA(allocator, parameters->textureProxy, args,
                                   parameters->alphaStart, &uvMatrix);
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

// A device-space AA RectEffect folds into the chain's dedicated AARectCoverage slot. Other AA
// rects lower through RectCoverage, while non-AA rects use the existing zero-radius RRectCoverage
// op with antiAlias=0, so no new shader variant is required.
static bool IsChainFoldableRectEffect(const FragmentProcessor* fp) {
  if (fp == nullptr || !IsProcessor<RectEffect>(fp) || fp->numChildProcessors() != 0) {
    return false;
  }
  auto rectEffect = static_cast<const RectEffect*>(fp);
  return rectEffect->isDeviceSpaceRect() && rectEffect->isAntiAlias();
}

// Analytic clip FPs folded onto the chain as color-root coverage slots (the two-FP coverage
// forms). Device-space AA rects use AARectCoverage, local-space AA rects use LocalRectCoverage,
// and rrects plus non-AA rects use RRectCoverage. The kernel carries two rect parameter sets per
// space and four rrect elements, so coverage exceeding those caps keeps the runtime route.
struct ChainNonAARect {
  Rect rect = {};
  Matrix deviceToLocal = Matrix::I();
};

struct ChainClipSlots {
  std::vector<const RectEffect*> deviceRects = {};
  std::vector<const RectEffect*> localRects = {};
  std::vector<const RRectEffect*> rrects = {};
  std::vector<ChainNonAARect> nonAARects = {};

  bool empty() const {
    return deviceRects.empty() && localRects.empty() && rrects.empty() && nonAARects.empty();
  }
};

// Collects analytic clip leaves, including composed clips, within the chain's parameter budgets.
// When maskEffect is given, one alpha-only, non-perspective DeviceSpaceTextureEffect inside the
// clip (a path clip's rasterized mask composed with analytic clips) is taken as the chain's mask
// child; maskEffect must be null on entry for that, so a second mask fails. Returns false if any
// leaf cannot be represented without materialization.
static bool CollectChainClipSlot(const FragmentProcessor* fp, ChainClipSlots* slots,
                                 const DeviceSpaceTextureEffect** maskEffect = nullptr) {
  if (fp == nullptr || slots == nullptr) {
    return false;
  }
  if (IsProcessor<ComposeFragmentProcessor>(fp)) {
    if (fp->numChildProcessors() == 0) {
      return false;
    }
    for (size_t index = 0; index < fp->numChildProcessors(); ++index) {
      if (!CollectChainClipSlot(fp->childProcessor(index), slots, maskEffect)) {
        return false;
      }
    }
    return true;
  }
  if (fp->numChildProcessors() != 0) {
    return false;
  }
  if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
    auto* dste = static_cast<const DeviceSpaceTextureEffect*>(fp);
    if (maskEffect == nullptr || *maskEffect != nullptr || !dste->isAlphaOnly() ||
        dste->hasPerspective()) {
      return false;
    }
    *maskEffect = dste;
    return true;
  }
  if (IsProcessor<RectEffect>(fp)) {
    auto* rectEffect = static_cast<const RectEffect*>(fp);
    if (!rectEffect->isAntiAlias()) {
      // A non-AA rect reuses the rrect op with zero radii and antiAlias disabled, so it shares the
      // four rrect elements' budget rather than the rect arrays.
      if (slots->nonAARects.size() + slots->rrects.size() >= 4) {
        return false;
      }
      slots->nonAARects.push_back({rectEffect->getRect(), rectEffect->getDeviceToLocal()});
      return true;
    }
    if (rectEffect->isDeviceSpaceRect()) {
      if (slots->deviceRects.size() >= 2) {
        return false;
      }
      slots->deviceRects.push_back(rectEffect);
    } else {
      if (slots->localRects.size() >= 2) {
        return false;
      }
      slots->localRects.push_back(rectEffect);
    }
    return true;
  }
  if (IsProcessor<RRectEffect>(fp)) {
    if (slots->rrects.size() + slots->nonAARects.size() >= 4) {
      return false;
    }
    slots->rrects.push_back(static_cast<const RRectEffect*>(fp));
    return true;
  }
  return false;
}

// Whether a Matrix::get9 row-major array is the identity matrix.
static bool IsIdentityMatrix(const std::array<float, 9>& values) {
  return values[0] == 1.0f && values[1] == 0.0f && values[2] == 0.0f && values[3] == 0.0f &&
         values[4] == 1.0f && values[5] == 0.0f && values[6] == 0.0f && values[7] == 0.0f &&
         values[8] == 1.0f;
}

// The data-only result of flattening one pointwise DAG pass (plus the draw's folded coverage) onto
// the fused chain kernel: the register-allocated instruction array, the texture leaves in sampler
// order and the resolved resource profile. PlanChainLayout produces it without creating any
// processor, so AOTPlanExecutor::CanExecute and the DAG planner run exactly the encoding, budget,
// profile and register checks construction runs; BuildChainFP then only creates the processors.
struct ChainLayout {
  std::vector<AOTChainSlot> slots = {};
  // Texture leaves in sampler order: leafNodes[k] pairs with slot k and TextureSampler_k.
  std::vector<const AOTEffectNode*> leafNodes = {};
  std::vector<AOTChainTiledLeaf> tiledLeaves = {};
  const AOTGradientParameters* lutGradient = nullptr;
  const AOTPerlinNoiseParameters* noise = nullptr;
  uint32_t coordSourceMask = ~0u;
  size_t rootRegister = 0;
  int coverageRootRegister = -1;
  int clipCoverageRegister = -1;
  // Indices into leafNodes of the device-space leaves, in leaf order.
  std::vector<int> deviceLeafIndices = {};
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
};

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
  const ChainClipSlots* clipSlots = nullptr;
  const AOTEffectGraph* coverageGraph = nullptr;
  AOTNodeID coverageRoot = AOTNodeID();
  bool coverageLeafFromUVCoord = false;
  bool checkKernel = false;
  ChainLayout* layout = nullptr;
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
  if (ctx->coverageGraph != nullptr && ctx->coverageRoot.index() >= ctx->covCount) {
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

// Stage three: encodes every scheduled node into its chain slot (the per-kind op, its parameter
// block, and its mapped input registers), collecting the texture leaves, the LUT gradient and the
// noise source into the layout along the way. Per-kind budgets that need no cross-node state are
// enforced here (the rect/rrect coverage capacities, the single gradient).
static bool EncodeChainSlots(ChainBuildContext* ctx) {
  auto* layout = ctx->layout;
  ctx->slots.assign(ctx->ordered.size(), AOTChainSlot{});
  size_t rectCoverageCount = 0;
  size_t localRectCoverageCount = 0;
  ctx->hasGradient = false;
  // All-ones reproduces the legacy behavior (every target reads the uvCoord attribute when the GP
  // carries one). The atlas-text route clears it and sets only the coverage leaves' bits.
  layout->coordSourceMask = ctx->coverageLeafFromUVCoord ? 0u : ~0u;
  layout->leafNodes.clear();
  layout->tiledLeaves.clear();
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
    auto& slot = ctx->slots[index];
    switch (node->kind) {
      case AOTEffectKind::TextureSource: {
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
          break;
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
          layout->coordSourceMask |= 1u << static_cast<uint32_t>(layout->leafNodes.size());
        }
        if (HasShaderTiling(*parameters)) {
          layout->tiledLeaves.push_back(
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
        break;
      }
      case AOTEffectKind::PerlinNoiseSource: {
        auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
        if (parameters == nullptr || layout->noise != nullptr || inputBase != 0 ||
            parameters->permutationsView == nullptr || parameters->noiseView == nullptr) {
          return false;
        }
        layout->noise = parameters;
        slot.op = AOTChainOp::PerlinNoise;
        slot.noise = *parameters;
        break;
      }
      case AOTEffectKind::ConstColor: {
        auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        slot.op = AOTChainOp::ConstColor;
        slot.constColor = *parameters;
        slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::ColorMatrix:
      case AOTEffectKind::Luma:
      case AOTEffectKind::AlphaThreshold:
      case AOTEffectKind::ColorSpaceXform: {
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
        break;
      }
      case AOTEffectKind::InputOpaque: {
        if (node->inputs.size() != 1) {
          return false;
        }
        slot.op = AOTChainOp::InputOpaque;
        slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::MulAlpha: {
        if (node->inputs.size() != 2) {
          return false;
        }
        slot.op = AOTChainOp::MulAlpha;
        slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
        slot.in1 = MapChainOpInput(ctx, inputBase, node->inputs[1]);
        break;
      }
      case AOTEffectKind::Blend: {
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
        break;
      }
      case AOTEffectKind::RectCoverage: {
        auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        if (IsIdentityMatrix(parameters->deviceToLocal)) {
          // The kernel carries two CoverageRect array elements per space (the largest clip stack
          // either space produces in practice); a third one keeps the runtime route.
          if (rectCoverageCount >= 2) {
            return false;
          }
          ++rectCoverageCount;
          slot.op = AOTChainOp::AARectCoverage;
          slot.rectCoverage = *parameters;
        } else {
          // Same for the local-space form through the CoverageLocalRect array.
          if (localRectCoverageCount >= 2) {
            return false;
          }
          ++localRectCoverageCount;
          slot.op = AOTChainOp::LocalRectCoverage;
          slot.localRectCoverage = *parameters;
        }
        slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::RRectCoverage: {
        auto parameters = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        // The kernel carries four CoverageRRect* array elements; ValidateSlots rejects a fifth
        // rrect slot.
        slot.op = AOTChainOp::RRectCoverage;
        slot.rrectCoverage = *parameters;
        slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
        break;
      }
      case AOTEffectKind::GradientSource: {
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
        break;
      }
      default:
        return false;
    }
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
    if (const auto* texture = std::get_if<AOTTextureParameters>(&node->parameters)) {
      ++colorDemand.leafBindings;
      if (HasShaderTiling(*texture)) {
        ++colorDemand.tiledRecipes;
      }
    } else if (const auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters)) {
      ++colorDemand.gradients;
      if (gradient->colorizerKind == 3) {
        ++colorDemand.leafBindings;
      }
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
      (ctx->hasGradient || layout->tiledLeaves.size() > ChainResourceCapacity.tiledRecipes)) {
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
  if (ctx->coverageGraph == nullptr) {
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

// Stage seven: appends the narrow clip slots (the analytic clip FPs the matcher collected). They
// never multiply the color root: they chain from the scalar unit one (-4, the innermost link) so
// their product is a pure clip coverage value the kernel reads through ClipCoverageRegister and
// composites as the XP's coverage input on top of the GP coverage — folding them into the source
// was only correct for SrcOver-class blending, and chaining them from the coverage unit (-3)
// squared the GP coverage. The color root stays the pre-clip result.
static void AppendChainClipSlot(ChainBuildContext* ctx, AOTChainSlot clipSlot) {
  clipSlot.in0 = ctx->clipChainInput;
  ctx->clipChainInput = static_cast<int>(ctx->slots.size());
  ctx->clipCoverageSlot = ctx->clipChainInput;
  ctx->slots.push_back(clipSlot);
}

static void AppendClipSlots(ChainBuildContext* ctx) {
  for (auto* deviceRect : ctx->clipSlots->deviceRects) {
    AOTChainSlot rectSlot = {};
    rectSlot.op = AOTChainOp::AARectCoverage;
    const auto& rect = deviceRect->getRect();
    rectSlot.rectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    AppendChainClipSlot(ctx, rectSlot);
  }
  for (auto* localRect : ctx->clipSlots->localRects) {
    AOTChainSlot localRectSlot = {};
    localRectSlot.op = AOTChainOp::LocalRectCoverage;
    const auto& rect = localRect->getRect();
    localRectSlot.localRectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& matrix = localRect->getDeviceToLocal();
    localRectSlot.localRectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2],
                                                     matrix[3], matrix[4], matrix[5],
                                                     matrix[6], matrix[7], matrix[8]};
    AppendChainClipSlot(ctx, localRectSlot);
  }
  for (const auto& nonAARect : ctx->clipSlots->nonAARects) {
    AOTChainSlot rectSlot = {};
    rectSlot.op = AOTChainOp::RRectCoverage;
    const auto& rect = nonAARect.rect;
    rectSlot.rrectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& matrix = nonAARect.deviceToLocal;
    rectSlot.rrectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                                            matrix[5], matrix[6], matrix[7], matrix[8]};
    rectSlot.rrectCoverage.antiAlias = 0.0f;
    AppendChainClipSlot(ctx, rectSlot);
  }
  for (auto* rrectEffect : ctx->clipSlots->rrects) {
    AOTChainSlot rrectSlot = {};
    rrectSlot.op = AOTChainOp::RRectCoverage;
    const auto& rect = rrectEffect->getLocalRect();
    rrectSlot.rrectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& radii = rrectEffect->getRadii();
    for (size_t i = 0; i < 4; ++i) {
      rrectSlot.rrectCoverage.radiiX[i] = radii[i].x;
      rrectSlot.rrectCoverage.radiiY[i] = radii[i].y;
    }
    const auto& matrix = rrectEffect->getDeviceToLocal();
    rrectSlot.rrectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                                             matrix[5], matrix[6], matrix[7], matrix[8]};
    rrectSlot.rrectCoverage.antiAlias = rrectEffect->isAntiAlias() ? 1.0f : 0.0f;
    AppendChainClipSlot(ctx, rrectSlot);
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
  for (const auto& tiled : layout->tiledLeaves) {
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
  layout->slots = std::move(ctx->slots);
  layout->rootRegister = static_cast<size_t>(ctx->assignment.rootRegister);
  layout->coverageRootRegister = ctx->assignment.coverageRootRegister;
  layout->clipCoverageRegister = ctx->assignment.clipCoverageRegister;
  layout->profile = ctx->finalProfile;
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
                            const ChainClipSlots& clipSlots, const AOTEffectGraph* coverageGraph,
                            AOTNodeID coverageRoot, bool coverageLeafFromUVCoord, bool checkKernel,
                            ChainLayout* layout) {
  ChainBuildContext ctx = {};
  ctx.graph = &graph;
  ctx.pass = &pass;
  ctx.clipSlots = &clipSlots;
  ctx.coverageGraph = coverageGraph;
  ctx.coverageRoot = coverageRoot;
  ctx.coverageLeafFromUVCoord = coverageLeafFromUVCoord;
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
  AppendClipSlots(&ctx);
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
                                      std::shared_ptr<TextureProxy>* firstProxy) {
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
  }
  return true;
}

// Fills the sampler slots up to the four-leaf artifacts' TEXTURE_COUNT with phantom children
// re-using the padding texture. A chain that already binds four (or none) is left alone.
static bool PadSamplersToFour(BlockAllocator* allocator, const ChainPaddingTexture& padding,
                              std::vector<PlacementPtr<FragmentProcessor>>* samplerPadding,
                              size_t* samplerChildren) {
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

static bool BuildChainMaskChild(BlockAllocator* allocator,
                                const DeviceSpaceTextureEffect* maskEffect,
                                const ChainPaddingTexture& padding, size_t samplerChildren,
                                ChainMaskChild* result) {
  if (maskEffect != nullptr) {
    // The mask child's Subset write routes to the kernel's DeviceMaskSubset decoy, so a leaf's
    // real subset rect survives in the shared uniform block.
    result->child = DeviceSpaceTextureEffect::Make(allocator, maskEffect->getTextureProxy(),
                                                   maskEffect->getUVMatrix());
    return result->child != nullptr;
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
  return result->child != nullptr;
}

// Rebuilds the fused chain processor from a planned layout: the texture leaves, the LUT and noise
// sampler-only children, the phantom padding the four-sampler artifacts need and the mask child.
// Every slot and budget decision was already taken by PlanChainLayout; a failure here can only
// come from a resource that cannot be wrapped into a processor.
static PlacementPtr<FragmentProcessor> BuildChainFP(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    const ChainClipSlots& clipSlots, const DeviceSpaceTextureEffect* maskEffect,
    const AOTEffectGraph* coverageGraph = nullptr, AOTNodeID coverageRoot = AOTNodeID(),
    bool coverageLeafFromUVCoord = false) {
  ChainLayout layout = {};
  if (!PlanChainLayout(graph, pass, clipSlots, coverageGraph, coverageRoot, coverageLeafFromUVCoord,
                       true, &layout)) {
    return nullptr;
  }
  std::vector<PlacementPtr<FragmentProcessor>> leaves = {};
  leaves.reserve(layout.leafNodes.size());
  for (auto* node : layout.leafNodes) {
    auto leaf = BuildTextureLeafFP(allocator, node);
    if (leaf == nullptr || !IsProcessor<TextureEffect>(leaf.get())) {
      return nullptr;
    }
    leaves.push_back(std::move(leaf));
  }
  // A LUT gradient carries its baked texture as a sampler-only child appended after the DAG
  // leaves; the OP_GRADIENT LUT branch reads it through the GradientLUTLeaf uniform.
  PlacementPtr<FragmentProcessor> lutChild = nullptr;
  int lutLeafIndex = -1;
  if (layout.lutGradient != nullptr) {
    SamplingOptions lutSampling(FilterMode::Linear, MipmapMode::None);
    SamplingArgs lutArgs = {TileMode::Clamp, TileMode::Clamp, lutSampling, SrcRectConstraint::Fast};
    auto lutMatrix = Matrix::I();
    lutChild = TextureEffect::Make(allocator, layout.lutGradient->lutProxy, lutArgs, &lutMatrix);
    if (lutChild == nullptr) {
      return nullptr;
    }
    lutLeafIndex = static_cast<int>(leaves.size());
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
      !AppendNoiseLookupChildren(allocator, layout.noise, &samplerPadding, &noisePaddingProxy)) {
    return nullptr;
  }
  auto padding =
      ChoosePaddingTexture(graph, pass, coverageGraph, layout.noise, std::move(noisePaddingProxy));
  size_t samplerChildren = leaves.size() + (lutChild != nullptr ? 1 : 0) + samplerPadding.size();
  if (!PadSamplersToFour(allocator, padding, &samplerPadding, &samplerChildren)) {
    return nullptr;
  }
  ChainMaskChild maskChildResult = {};
  if (!BuildChainMaskChild(allocator, maskEffect, padding, samplerChildren, &maskChildResult)) {
    return nullptr;
  }
  const auto& tiledLeaves = layout.tiledLeaves;
  const int tiledLeafIndex = tiledLeaves.empty() ? -1 : tiledLeaves[0].leafIndex;
  const int tiledLeafIndex2 = tiledLeaves.size() < 2 ? -1 : tiledLeaves[1].leafIndex;
  const AOTTiledTextureRecipe* recipePtr = tiledLeaves.empty() ? nullptr : &tiledLeaves[0].recipe;
  const AOTTiledTextureRecipe* recipePtr2 =
      tiledLeaves.size() < 2 ? nullptr : &tiledLeaves[1].recipe;
  std::vector<AOTChainTiledLeaf> additionalTiledLeaves = {};
  if (tiledLeaves.size() > 2) {
    additionalTiledLeaves.assign(tiledLeaves.begin() + 2, tiledLeaves.end());
  }
  std::vector<AOTChainDeviceLeaf> deviceLeaves = {};
  deviceLeaves.reserve(layout.deviceLeafIndices.size());
  for (auto leafIndex : layout.deviceLeafIndices) {
    const auto* parameters = std::get_if<AOTTextureParameters>(
        &layout.leafNodes[static_cast<size_t>(leafIndex)]->parameters);
    if (parameters == nullptr) {
      return nullptr;
    }
    deviceLeaves.push_back({leafIndex, parameters->textureProxy, parameters->uvMatrix});
  }
  return AOTPointwiseChainProcessor::Make(
      allocator, std::move(leaves), layout.slots, layout.rootRegister, tiledLeafIndex, recipePtr,
      std::move(maskChildResult.child), layout.coverageRootRegister, layout.coordSourceMask,
      std::move(lutChild), lutLeafIndex, std::move(samplerPadding), maskChildResult.isPhantom,
      layout.clipCoverageRegister, tiledLeafIndex2, recipePtr2, layout.profile,
      additionalTiledLeaves, deviceLeaves);
}

}  // namespace

static bool LowerAnalyticCoverageFP(const FragmentProcessor* fp,
                                    const DeviceSpaceTextureEffect** maskEffect,
                                    AOTNodeBuilder* covBuilder, AOTNodeID* covRoot) {
  if (fp == nullptr || maskEffect == nullptr || covBuilder == nullptr || covRoot == nullptr) {
    return false;
  }
  std::vector<const FragmentProcessor*> leaves = {};
  if (IsProcessor<ComposeFragmentProcessor>(fp)) {
    for (size_t i = 0; i < fp->numChildProcessors(); ++i) {
      leaves.push_back(fp->childProcessor(i));
    }
  } else {
    leaves.push_back(fp);
  }
  std::vector<const FragmentProcessor*> analytic = {};
  for (auto* leaf : leaves) {
    if (IsProcessor<DeviceSpaceTextureEffect>(leaf)) {
      auto* dste = static_cast<const DeviceSpaceTextureEffect*>(leaf);
      if (*maskEffect != nullptr || !dste->isAlphaOnly() || dste->hasPerspective()) {
        return false;
      }
      *maskEffect = dste;
      continue;
    }
    analytic.push_back(leaf);
  }
  if (analytic.empty()) {
    return false;
  }
  AOTNodeID unit = AOTNodeID::Invalid();
  if (!covBuilder->addGeometryCoverage(&unit)) {
    return false;
  }
  AOTNodeID current = unit;
  for (auto* leaf : analytic) {
    if ((!IsProcessor<RectEffect>(leaf) && !IsProcessor<RRectEffect>(leaf)) ||
        leaf->numChildProcessors() != 0) {
      return false;
    }
    AOTNodeID next = AOTNodeID::Invalid();
    if (!leaf->lowerToAOT(covBuilder, current, &next)) {
      return false;
    }
    current = next;
  }
  if (current == unit) {
    return false;
  }
  *covRoot = current;
  return true;
}

bool AOTChainBuilder::PlanChainPass(const AOTEffectGraph& graph, const AOTPassDescriptor& pass) {
  ChainLayout layout = {};
  return PlanChainLayout(graph, pass, {}, nullptr, AOTNodeID(), false, true, &layout);
}

bool AOTChainBuilder::ResolveChainProfile(const AOTEffectGraph& graph,
                                          const AOTPassDescriptor& pass,
                                          AOTResourceProfile* profile) {
  ChainLayout layout = {};
  if (profile == nullptr ||
      !PlanChainLayout(graph, pass, {}, nullptr, AOTNodeID(), false, false, &layout)) {
    return false;
  }
  *profile = layout.profile;
  return true;
}

// Single-FP analytic coverage beyond the narrow forms: a bare AA RectEffect (local space), a bare
// RRectEffect, or a flat Compose of AA rect / rrect leaves with at most one alpha-only device-space
// mask (extracted as the mask child — the kernel cannot sample a device-space leaf inside the
// coverage subtree). The analytic leaves lower through the AOT graph as chained RectCoverage /
// RRectCoverage nodes rooted at the GP coverage unit, which is what makes composed multi-clip
// coverage representable at all. Returns false when the coverage carries anything else.

bool AOTChainBuilder::BuildLinearSegment(const AOTEffectGraph& graph,
                                         const std::vector<AOTNodeID>& operators,
                                         const AOTTextureParameters& source,
                                         AOTEffectGraph* segment, AOTPassDescriptor* pass,
                                         bool firstPass) {
  if (segment == nullptr || pass == nullptr) {
    return false;
  }
  AOTNodeBuilder builder = {};
  AOTNodeID geometry = AOTNodeID::Invalid();
  AOTNodeID current = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&geometry) ||
      (!firstPass && !builder.addGeometryWhiteInput(&current))) {
    return false;
  }
  if (firstPass) {
    current = geometry;
  }
  if (!builder.addTextureSource(current, source, &current)) {
    return false;
  }
  for (auto nodeID : operators) {
    const auto* node = graph.nodeAt(nodeID);
    if (node == nullptr || node->inputs.size() != 1) {
      return false;
    }
    bool added = false;
    if (const auto* matrix = std::get_if<AOTColorMatrixParameters>(&node->parameters)) {
      added = node->kind == AOTEffectKind::ColorMatrix &&
              builder.addColorMatrix(current, *matrix, &current);
    } else if (const auto* luma = std::get_if<AOTLumaParameters>(&node->parameters)) {
      added = node->kind == AOTEffectKind::Luma && builder.addLuma(current, *luma, &current);
    } else if (const auto* threshold =
                   std::get_if<AOTAlphaThresholdParameters>(&node->parameters)) {
      added = node->kind == AOTEffectKind::AlphaThreshold &&
              builder.addAlphaThreshold(current, *threshold, &current);
    } else if (const auto* xform = std::get_if<AOTColorSpaceXformParameters>(&node->parameters)) {
      added = node->kind == AOTEffectKind::ColorSpaceXform &&
              builder.addColorSpaceXform(current, *xform, &current);
    }
    if (!added) {
      return false;
    }
  }
  AOTEffectGraph result = {};
  if (!builder.finish(current, &result)) {
    return false;
  }
  AOTPassDescriptor resultPass = {};
  for (uint32_t index = firstPass ? 1 : 2; index < result.nodeCount(); ++index) {
    // GeometryWhiteInput is a designator, not an instruction slot.
    resultPass.nodes.push_back(AOTNodeID(index));
  }
  resultPass.output = result.root();
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!ResolveChainProfile(result, resultPass, &profile)) {
    return false;
  }
  resultPass.resourceProfile = profile;
  resultPass.kernel =
      IsAOTGraphProfile(profile) ? AOTKernelKind::PointwiseGraph : AOTKernelKind::PointwiseChain;
  *segment = std::move(result);
  *pass = std::move(resultPass);
  return true;
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

// Copies one chain node into the builder, resolving its input edges through builtMap (and the cut
// replacements). The split-off fill's coordinate remap: it evaluates at top-left device positions
// while the original draw evaluated at its own coordinate spaces, so every local-coordinate
// parameter (the gradient coordMatrix, the noise uvMatrix, a plain/tiled texture leaf's uvMatrix)
// is precomposed with coordToLocal, and a device-space texture leaf's uvMatrix (which reads the
// destination's fragment coordinates) with deviceToFrag; the device-space coverage parameters stay
// as they are (the fill's positions are the device ones). A terminal pass passes identity for
// both, because it runs on the destination and sees the original spaces.
static bool CopyChainNode(const AOTEffectGraph& graph, AOTNodeID nodeID,
                          const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                          const std::vector<AOTNodeID>& cutRoots,
                          const std::vector<AOTNodeID>& cutLeaves, const Matrix& coordToLocal,
                          const Matrix& deviceToFrag, AOTNodeBuilder* builder, AOTNodeID* copied) {
  const auto* node = graph.nodeAt(nodeID);
  if (node == nullptr || builder == nullptr || copied == nullptr) {
    return false;
  }
  switch (node->kind) {
    case AOTEffectKind::GeometryColorOpaqueInput: {
      if (!node->inputs.empty()) {
        return false;
      }
      return builder->addGeometryColorOpaqueInput(copied);
    }
    case AOTEffectKind::GeometryWhiteInput: {
      if (!node->inputs.empty()) {
        return false;
      }
      return builder->addGeometryWhiteInput(copied);
    }
    case AOTEffectKind::TextureSource: {
      auto* parameters = std::get_if<AOTTextureParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      auto texture = *parameters;
      if (texture.samplingKind == AOTTextureSamplingKind::Device) {
        texture.uvMatrix.preConcat(deviceToFrag);
      } else {
        texture.uvMatrix.preConcat(coordToLocal);
      }
      return builder->addTextureSource(input, texture, copied);
    }
    case AOTEffectKind::ColorMatrix: {
      auto* parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addColorMatrix(input, *parameters, copied);
    }
    case AOTEffectKind::Luma: {
      auto* parameters = std::get_if<AOTLumaParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addLuma(input, *parameters, copied);
    }
    case AOTEffectKind::AlphaThreshold: {
      auto* parameters = std::get_if<AOTAlphaThresholdParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addAlphaThreshold(input, *parameters, copied);
    }
    case AOTEffectKind::ColorSpaceXform: {
      auto* parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addColorSpaceXform(input, *parameters, copied);
    }
    case AOTEffectKind::ConstColor: {
      auto* parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addConstColor(input, *parameters, copied);
    }
    case AOTEffectKind::Blend: {
      auto* parameters = std::get_if<AOTBlendParameters>(&node->parameters);
      auto srcInput = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      auto dstInput = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[1]);
      if (parameters == nullptr || node->inputs.size() != 2 || !srcInput.isValid() ||
          !dstInput.isValid()) {
        return false;
      }
      return builder->addBlend(srcInput, dstInput, *parameters, copied);
    }
    case AOTEffectKind::PerlinNoiseSource: {
      auto* parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      auto noise = *parameters;
      noise.uvMatrix.preConcat(coordToLocal);
      return builder->addPerlinNoiseSource(input, noise, copied);
    }
    case AOTEffectKind::GradientSource: {
      auto* parameters = std::get_if<AOTGradientParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      auto gradient = *parameters;
      gradient.coordMatrix.preConcat(coordToLocal);
      return builder->addGradientSource(input, gradient, copied);
    }
    case AOTEffectKind::RectCoverage: {
      auto* parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addRectCoverage(input, *parameters, copied);
    }
    case AOTEffectKind::RRectCoverage: {
      auto* parameters = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (parameters == nullptr || node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addRRectCoverage(input, *parameters, copied);
    }
    case AOTEffectKind::InputOpaque: {
      auto input = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      if (node->inputs.size() != 1 || !input.isValid()) {
        return false;
      }
      return builder->addInputOpaque(input, copied);
    }
    case AOTEffectKind::MulAlpha: {
      auto valueInput = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[0]);
      auto alphaInput = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, node->inputs[1]);
      if (node->inputs.size() != 2 || !valueInput.isValid() || !alphaInput.isValid()) {
        return false;
      }
      return builder->addMulAlpha(valueInput, alphaInput, copied);
    }
    default:
      return false;
  }
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
                           const Matrix& coordToLocal, const Matrix& deviceToFrag,
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
    bool isDesignator = node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
                        node->kind == AOTEffectKind::GeometryWhiteInput;
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
                       deviceToFrag, builder, &copied)) {
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
  // have added nodes the caller did not list (the replacement texture leaf), exactly like
  // BuildLinearSegment lists its whole segment graph.
  for (uint32_t index = 1; index < built.nodeCount(); ++index) {
    auto* node = built.nodeAt(AOTNodeID(index));
    if (node == nullptr || node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
        node->kind == AOTEffectKind::GeometryWhiteInput) {
      continue;
    }
    resultPass.nodes.push_back(AOTNodeID(index));
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!AOTChainBuilder::ResolveChainProfile(built, resultPass, &profile)) {
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
                                      const Matrix& coordToLocal, const Matrix& deviceToFrag,
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
                      deviceToFrag, &builder, &builtMap)) {
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
    return BuildChainFP(allocator, graph, pass, {}, nullptr);
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
      auto source = BuildTextureLeafFP(allocator, sourceNode);
      if (source == nullptr || !IsProcessor<TextureEffect>(source.get())) {
        return nullptr;
      }
      return AOTYUVChainProcessor::Make(allocator, std::move(source), slots);
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
  ChainClipSlots clipSlots = {};
  if (coverageFPs.empty()) {
    return BuildChainFP(allocator, graph, pass, clipSlots, nullptr);
  }
  if (coverageFPs.size() > 2) {
    return nullptr;
  }
  // Normalize the coverage into the forms the chain kernel carries. The narrow forms come first:
  // a bare device-space AA RectEffect folds into an AARectCoverage slot, an alpha-only
  // DeviceSpaceTextureEffect becomes the mask child, and Compose(mask, rect) yields both.
  // Analytic coverage the narrow forms cannot express — rrects, local-space rects, composed
  // multi-clip chains — lowers through the AOT graph as RectCoverage / RRectCoverage nodes rooted
  // at the GP coverage unit. Anything else lowers as a general coverage subtree. A two-FP coverage
  // folds as [analytic slots / mask, texture subtree] or [texture subtree, analytic slots / mask]
  // depending on which position carries the subtree.
  const DeviceSpaceTextureEffect* maskEffect = nullptr;
  const FragmentProcessor* subtreeFP = nullptr;
  if (coverageFPs.size() == 1) {
    auto* coverage = coverageFPs.front();
    auto coverageName = coverage->name();
    if (coverageName == "RectEffect" && IsChainFoldableRectEffect(coverage)) {
      clipSlots.deviceRects.push_back(static_cast<const RectEffect*>(coverage));
      return BuildChainFP(allocator, graph, pass, clipSlots, nullptr);
    }
    if (coverageName == "DeviceSpaceTextureEffect") {
      maskEffect = static_cast<const DeviceSpaceTextureEffect*>(coverage);
      if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
        return nullptr;
      }
      return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect);
    }
    if (coverageName == "ComposeFragmentProcessor" && coverage->numChildProcessors() == 2 &&
        IsProcessor<DeviceSpaceTextureEffect>(coverage->childProcessor(0)) &&
        IsChainFoldableRectEffect(coverage->childProcessor(1))) {
      maskEffect = static_cast<const DeviceSpaceTextureEffect*>(coverage->childProcessor(0));
      clipSlots.deviceRects.push_back(static_cast<const RectEffect*>(coverage->childProcessor(1)));
      if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
        return nullptr;
      }
      return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect);
    }
    AOTNodeBuilder covBuilder = {};
    AOTNodeID covRoot = AOTNodeID::Invalid();
    if (LowerAnalyticCoverageFP(coverage, &maskEffect, &covBuilder, &covRoot)) {
      AOTEffectGraph covGraph = {};
      if (!covBuilder.finish(covRoot, &covGraph)) {
        return nullptr;
      }
      return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect, &covGraph, covRoot,
                          coverageLeafFromUVCoord);
    }
    subtreeFP = coverage;
  } else {
    // Two-FP coverage: the terminal FP decides the form. A trailing alpha-only device mask keeps
    // the mask-child role and the first FP lowers as the subtree (or folds as an analytic slot);
    // a trailing local texture is the subtree root (a bare local mask, accepted by the gate only
    // when the texture is already instantiated), with the first FP folding as analytic clip slots
    // or the mask child; a trailing analytic clip leaf folds as clip slots with the first FP
    // lowering as the subtree.
    auto* last = coverageFPs.back();
    auto* first = coverageFPs.front();
    if (IsProcessor<DeviceSpaceTextureEffect>(last)) {
      maskEffect = static_cast<const DeviceSpaceTextureEffect*>(last);
      if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
        return nullptr;
      }
      if (IsProcessor<RectEffect>(first) || IsProcessor<RRectEffect>(first)) {
        if (!CollectChainClipSlot(first, &clipSlots)) {
          return nullptr;
        }
      } else {
        subtreeFP = first;
      }
    } else if (IsProcessor<TextureEffect>(last)) {
      subtreeFP = last;
      if (IsProcessor<RectEffect>(first) || IsProcessor<RRectEffect>(first) ||
          IsProcessor<ComposeFragmentProcessor>(first)) {
        if (!CollectChainClipSlot(first, &clipSlots, &maskEffect)) {
          return nullptr;
        }
      } else if (IsProcessor<DeviceSpaceTextureEffect>(first)) {
        maskEffect = static_cast<const DeviceSpaceTextureEffect*>(first);
        if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
          return nullptr;
        }
      } else {
        return nullptr;
      }
    } else if (IsProcessor<RectEffect>(last) || IsProcessor<RRectEffect>(last) ||
               IsProcessor<ComposeFragmentProcessor>(last)) {
      // A composed analytic clip in the trailing position: the mask filter (or another subtree)
      // comes first and the clip stack folds into the clip slots after it. A path clip's device
      // mask inside the composed clip becomes the mask child.
      if (!CollectChainClipSlot(last, &clipSlots, &maskEffect)) {
        return nullptr;
      }
      subtreeFP = first;
    } else {
      return nullptr;
    }
  }
  if (subtreeFP == nullptr) {
    // A two-FP [analytic slots, device mask] coverage carries no texture subtree; the slots and
    // the mask child are the whole coverage.
    if (clipSlots.empty()) {
      return nullptr;
    }
    return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect);
  }
  // General coverage subtree: lower the FP with the GP coverage unit as its input. BuildChainFP
  // applies the acceptance gate (single root blend consuming the unit) before merging.
  AOTNodeBuilder covBuilder = {};
  AOTNodeID unit = AOTNodeID::Invalid();
  AOTNodeID covRoot = AOTNodeID::Invalid();
  // Strict lowering: a texture whose view is not yet instantiated (a generated mask) fails here,
  // which keeps the draw on the runtime route whose program builder emits a zero stub for such
  // textures — the AOT path must not serve content the runtime would have dropped.
  if (!covBuilder.addGeometryCoverage(&unit) ||
      !subtreeFP->lowerToAOT(&covBuilder, unit, &covRoot) || !covRoot.isValid() ||
      covRoot == unit) {
    return nullptr;
  }
  AOTEffectGraph covGraph = {};
  if (!covBuilder.finish(covRoot, &covGraph)) {
    return nullptr;
  }
  return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect, &covGraph, covRoot,
                      coverageLeafFromUVCoord);
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
  const bool atlasText = IsProcessor<AtlasTextGeometryProcessor>(gp);
  const size_t coverageCount =
      programInfo->numFragmentProcessors() - programInfo->numColorFragmentProcessors();
  if (coverageCount > (atlasText ? 1u : 2u)) {
    return nullptr;
  }
  // The atlas glyph mask joins as a synthesized coverage leaf sourced from the maskCoord
  // attribute: only the alpha-only atlas readback is expressible — a color atlas replaces the
  // output color, which the chain cannot carry.
  std::vector<PlacementPtr<FragmentProcessor>> ownedCoverageFPs = {};
  std::vector<const FragmentProcessor*> coverageFPs = {};
  coverageFPs.reserve(coverageCount + 1);
  for (size_t index = programInfo->numColorFragmentProcessors();
       index < programInfo->numFragmentProcessors(); ++index) {
    coverageFPs.push_back(programInfo->getFragmentProcessor(index));
  }
  if (atlasText) {
    auto* atlasGP = static_cast<const AtlasTextGeometryProcessor*>(gp);
    auto atlasProxy = atlasGP->getTextureProxy();
    if (atlasProxy == nullptr || !atlasProxy->isAlphaOnly()) {
      return nullptr;
    }
    auto state = atlasGP->getSamplerState();
    SamplingOptions sampling(state.minFilterMode, state.magFilterMode, state.mipmapMode);
    SamplingArgs args = {TileMode::Clamp, TileMode::Clamp, sampling, SrcRectConstraint::Fast};
    // Identity: CoordTransform::getTotalMatrix applies the texture normalization itself, so the
    // leaf maps maskCoord straight into the atlas's normalized space.
    auto uvMatrix = Matrix::I();
    auto atlasLeaf = TextureEffect::Make(allocator, std::move(atlasProxy), args, &uvMatrix);
    if (atlasLeaf == nullptr) {
      return nullptr;
    }
    coverageFPs.push_back(atlasLeaf.get());
    ownedCoverageFPs.push_back(std::move(atlasLeaf));
  }
  std::vector<const FragmentProcessor*> colorProcessors = {};
  colorProcessors.reserve(programInfo->numColorFragmentProcessors() + 1);
  for (size_t index = 0; index < programInfo->numColorFragmentProcessors(); ++index) {
    colorProcessors.push_back(programInfo->getFragmentProcessor(index));
  }
  PlacementPtr<FragmentProcessor> unitColor = nullptr;
  if (colorProcessors.empty()) {
    // A color-free draw (pure mask/clip fill) takes the paint color as-is, which is a one-slot
    // const-color chain modulating the geometry color.
    unitColor = ConstColorProcessor::Make(allocator, PMColor::White(), InputMode::ModulateRGBA);
    if (unitColor == nullptr) {
      return nullptr;
    }
    colorProcessors.push_back(unitColor.get());
  }
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  if (!AOTRouter::PlanInlineRewrite(colorProcessors, &graph, &plan) ||
      !IsAOTPointwiseDAGKernel(plan.passes[0].kernel)) {
    // The perlin kernels have their own families; the chain family only takes the DAG kernels.
    return nullptr;
  }
  return BuildChainProcessor(allocator, graph, plan.passes[0], coverageFPs,
                             !ownedCoverageFPs.empty());
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
  if (!AOTRouter::PlanInlineRewrite(colorProcessors, &graph, &plan) ||
      plan.passes[0].kernel != AOTKernelKind::PerlinNoiseFill) {
    return nullptr;
  }
  return BuildFPForPass(allocator, graph, plan.passes[0]);
}

}  // namespace tgfx
