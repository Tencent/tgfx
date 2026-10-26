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
#include <utility>
#include <vector>
#include "gpu/AOTChainRegisterAllocator.h"
#include "gpu/ProxyProvider.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AOTYUVChainProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
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
  if (parameters.textureProxy == nullptr || parameters.isYUV) {
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
  if (layout == nullptr || pass.nodes.empty() || !pass.output.isValid() ||
      (checkKernel && !IsAOTPointwiseDAGKernel(pass.kernel))) {
    return false;
  }
  // Combined node space: color-graph nodes occupy [0, colorCount), coverage-graph nodes follow.
  const size_t colorCount = graph.nodeCount();
  const size_t covCount = coverageGraph != nullptr ? coverageGraph->nodeCount() : 0;
  if (coverageGraph != nullptr && coverageRoot.index() >= covCount) {
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
  // Every pass node must exist and appear once: a duplicate would encode the same node twice and
  // leave the first copy's consumers reading a stale register.
  std::vector<bool> inPass(colorCount, false);
  for (auto nodeID : pass.nodes) {
    if (nodeID.index() >= colorCount || inPass[nodeID.index()]) {
      return false;
    }
    inPass[nodeID.index()] = true;
  }
  if (pass.output.index() >= colorCount || !inPass[pass.output.index()]) {
    return false;
  }
  std::vector<size_t> slotOf(nodes.size(), SIZE_MAX);
  // For a computed-input texture node, the leading raw-sampling slot's position (sampler
  // binding); the node's value for consumers is the topologically-ordered TEX_MODULATE slot
  // tracked in slotOf.
  std::vector<size_t> rawSlotOf(nodes.size(), SIZE_MAX);
  std::vector<size_t> ordered = {};
  ordered.reserve(pass.nodes.size() + covCount);
  // A texture whose input is a computed node (not one of the designators): its sampling needs
  // no computed value, but its modulation must follow the producer, so the node occupies a raw
  // sampling slot in the leading block plus a TEX_MODULATE slot in the op phase.
  auto hasComputedInput = [&](size_t combined) {
    auto* node = nodes[combined];
    if (node->kind != AOTEffectKind::TextureSource || node->inputs.empty()) {
      return false;
    }
    const size_t inputBase = combined < colorCount ? 0 : colorCount;
    const size_t inputIndex = inputBase + node->inputs[0].index();
    if (inputIndex >= nodes.size()) {
      return false;
    }
    auto* inputNode = nodes[inputIndex];
    return inputNode->kind != AOTEffectKind::GeometryColor &&
           inputNode->kind != AOTEffectKind::GeometryColorOpaqueInput &&
           inputNode->kind != AOTEffectKind::GeometryWhiteInput &&
           inputNode->kind != AOTEffectKind::GeometryCoverage;
  };
  // Texture leaves first (color pass, then coverage subtree), then the remaining nodes in
  // topological order (color ops, then coverage ops). The two subtrees never reference each
  // other, so this layout keeps every input ahead of its consumer. A computed-input texture
  // appears in both phases: the texture phase holds its raw sampling slot, the op phase its
  // TEX_MODULATE instruction (slotOf points there, so consumers reference the modulated value).
  auto collectNode = [&](size_t combined, bool texturePhase) {
    if (nodes[combined]->kind == AOTEffectKind::TextureSource) {
      if (texturePhase) {
        rawSlotOf[combined] = ordered.size();
        ordered.push_back(combined);
        if (!hasComputedInput(combined)) {
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
  auto collect = [&](bool texturePhase) {
    for (auto nodeID : pass.nodes) {
      collectNode(nodeID.index(), texturePhase);
    }
    // Index 0 of the coverage graph is the GeometryCoverage unit, which never becomes a slot.
    // Designator-only input nodes (white / opaque geometry inputs lowered by xfer children) stay
    // out too: their value rides on the input-edge designators, not on a chain slot.
    for (size_t index = 1; index < covCount; ++index) {
      auto kind = nodes[colorCount + index]->kind;
      if (kind != AOTEffectKind::GeometryWhiteInput &&
          kind != AOTEffectKind::GeometryColorOpaqueInput) {
        collectNode(colorCount + index, texturePhase);
      }
    }
  };
  collect(true);
  collect(false);
  std::vector<AOTChainSlot> slots(ordered.size());
  size_t rectCoverageCount = 0;
  size_t localRectCoverageCount = 0;
  bool hasGradient = false;
  // All-ones reproduces the legacy behavior (every target reads the uvCoord attribute when the GP
  // carries one). The atlas-text route clears it and sets only the coverage leaves' bits.
  layout->coordSourceMask = coverageLeafFromUVCoord ? 0u : ~0u;
  layout->leafNodes.clear();
  layout->tiledLeaves.clear();
  layout->lutGradient = nullptr;
  layout->noise = nullptr;
  layout->deviceLeafIndices.clear();
  for (size_t index = 0; index < ordered.size(); ++index) {
    const size_t combined = ordered[index];
    auto node = nodes[combined];
    const size_t inputBase = combined < colorCount ? 0 : colorCount;
    for (auto input : node->inputs) {
      if (inputBase + input.index() >= (combined < colorCount ? colorCount : nodes.size())) {
        return false;
      }
    }
    auto mapInput = [&](AOTNodeID input) {
      return MapChainInput(nodes, slotOf, inputBase + input.index());
    };
    auto& slot = slots[index];
    switch (node->kind) {
      case AOTEffectKind::TextureSource: {
        auto* parameters = std::get_if<AOTTextureParameters>(&node->parameters);
        if (parameters == nullptr || !IsChainTextureLeaf(*parameters)) {
          return false;
        }
        if (index != rawSlotOf[combined]) {
          // Op-phase entry of a computed-input texture: the explicit modulate instruction.
          // Its input is the producing node's register (any computed node — an upstream
          // texture, a gradient, an operator), and its source is the raw sample prefetched
          // into chainLeafTex at the leading slot. The two modulations mirror the runtime
          // TextureEffect readback: alpha-only is sample.a * inputColor, RGBA is
          // sample * inputColor.a.
          slot.op = AOTChainOp::TexModulate;
          slot.in0 = mapInput(node->inputs[0]);
          slot.texModulateSourceSlot = static_cast<int>(rawSlotOf[combined]);
          slot.texModulateAlphaOnly = parameters->isAlphaOnly ? 1 : 0;
          // A negative producer is a designator after all (hasComputedInput disagreed with
          // mapInput) or is not part of this pass; both are build errors.
          if (slot.in0 < 0 || slot.texModulateSourceSlot >= MaxFusedAOTSamplers) {
            return false;
          }
          break;
        }
        slot.op = AOTChainOp::Texture;
        // Texture-phase entry of a computed-input texture is raw sampling only: the modulation
        // lives in the op-phase TEX_MODULATE slot, so no flags here, and the result register
        // stays dead because only the modulate instruction reads the prefetched sample.
        const bool rawSamplingOnly = slotOf[combined] != index;
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
              combined >= colorCount && !node->inputs.empty() && node->inputs[0].index() == 0;
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
              !node->inputs.empty() && nodes[inputBase + node->inputs[0].index()]->kind ==
                                           AOTEffectKind::GeometryColorOpaqueInput;
          if (slot.textureAlphaOnly != 0 && inputIsGeometryColor) {
            slot.textureModulate = 0;
            slot.textureModulateFullInput = 1;
          } else if (slot.textureAlphaOnly != 0 && inputIsOpaqueDesignator) {
            slot.textureModulateGeometryRGB = 1;
          }
        }
        if (coverageLeafFromUVCoord && inputBase != 0) {
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
        slot.in0 = mapInput(node->inputs[0]);
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
        if (!BuildPointwiseSlot(graph, node, &pointwise)) {
          return false;
        }
        // AOTPointwiseOpType and AOTChainOp share values 0..4 by ABI (both mirror the OP_*
        // constants), so the pointwise kinds convert directly.
        slot.op = static_cast<AOTChainOp>(static_cast<int>(pointwise.type));
        slot.colorMatrix = pointwise.colorMatrix;
        slot.luma = pointwise.luma;
        slot.alphaThreshold = pointwise.alphaThreshold;
        slot.colorSpaceXform = pointwise.colorSpaceXform;
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::InputOpaque: {
        if (node->inputs.size() != 1) {
          return false;
        }
        slot.op = AOTChainOp::InputOpaque;
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::MulAlpha: {
        if (node->inputs.size() != 2) {
          return false;
        }
        slot.op = AOTChainOp::MulAlpha;
        slot.in0 = mapInput(node->inputs[0]);
        slot.in1 = mapInput(node->inputs[1]);
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
        slot.in0 = mapInput(node->inputs[0]);
        slot.in1 = mapInput(node->inputs[1]);
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
        slot.in0 = mapInput(node->inputs[0]);
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
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::GradientSource: {
        auto parameters = std::get_if<AOTGradientParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return false;
        }
        // Single-authority budget: AOTPointwiseChainProcessor::MaxGradientSlots (the kernel
        // carries one chain-wide gradient parameter block and one SourceCoords varying).
        if (hasGradient) {
          return false;
        }
        hasGradient = true;
        if (parameters->colorizerKind == 3) {
          // A LUT gradient binds its baked texture as a sampler-only child after the DAG leaves.
          if (parameters->lutProxy == nullptr) {
            return false;
          }
          layout->lutGradient = parameters;
        }
        slot.op = AOTChainOp::Gradient;
        slot.gradient = *parameters;
        slot.in0 = mapInput(node->inputs[0]);
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
  // The pass kernel and its declared profile describe the color pass alone; the draw's coverage
  // may only raise the final profile (a coverage gradient or a third tiled leaf moves a Chain pass
  // onto the TiledGraph family, whose budgets are a superset for those fields).
  AOTResourceDemand colorDemand = {};
  for (auto nodeID : pass.nodes) {
    const auto* node = nodes[nodeID.index()];
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
  auto declaredProfile = pass.resourceProfile;
  if (declaredProfile == AOTResourceProfile::Automatic) {
    declaredProfile = SelectAOTResourceProfile(colorDemand);
  }
  if (!FitsAOTResourceProfile(colorDemand, declaredProfile) ||
      (checkKernel &&
       IsAOTGraphProfile(declaredProfile) != (pass.kernel == AOTKernelKind::PointwiseGraph))) {
    return false;
  }
  auto finalProfile = declaredProfile;
  if (finalProfile == AOTResourceProfile::Chain &&
      (hasGradient || layout->tiledLeaves.size() > ChainResourceCapacity.tiledRecipes)) {
    if (coverageGraph == nullptr) {
      return false;
    }
    finalProfile = AOTResourceProfile::TiledGraph;
  }
  const size_t leafCount = layout->leafNodes.size();
  if ((layout->lutGradient != nullptr && leafCount >= 2) ||
      (layout->noise != nullptr && (hasGradient || leafCount > 2 || coverageLeafFromUVCoord))) {
    return false;
  }
  size_t rootIndex = slotOf[pass.output.index()];
  if (rootIndex == SIZE_MAX) {
    return false;
  }
  // The clip-coverage channel (kernel: ClipCoverageRegister). The narrow clip slots chain from
  // the scalar unit one (-4) through clipChainInput — NOT from the coverage unit (-3): the tail
  // multiplies the clip product in on top of the GP coverage (vCoverage / gpPixelCoverage /
  // the coverage root), so a clip chain that already starts from the GP coverage would square it
  // (q * (q * clip)). The clip slots' product is therefore a pure clip value. Early-fold clips
  // arrive differently — the fold routes the clip coverage into the color graph as a
  // RectCoverage/RRectCoverage trailing suffix multiplying the root. Geometric coverage must ride
  // the XP's coverage input instead of the source (), so pop that suffix and rewire it like the
  // narrow clip slots: the innermost link chains from the unit one, outer links chain upward, the
  // color root moves to the node under the suffix, and the outermost link's register becomes the
  // clip-coverage value.
  int clipCoverageSlot = -1;
  int clipChainInput = -4;
  {
    // Walk in NODE-ordinal space (slotOf maps nodes to slots); the suffix stays in the color
    // chain when the node under it is a designator with no slot (e.g. a bare clip over the
    // geometry color) — that shape keeps the old source-fold form, which the entry guards
    // restrict to SrcOver-class blending.
    std::vector<size_t> suffixSlots = {};
    size_t walkNode = pass.output.index();
    while (walkNode < colorCount && (nodes[walkNode]->kind == AOTEffectKind::RectCoverage ||
                                     nodes[walkNode]->kind == AOTEffectKind::RRectCoverage)) {
      if (nodes[walkNode]->inputs.empty()) {
        break;
      }
      suffixSlots.push_back(slotOf[walkNode]);
      walkNode = nodes[walkNode]->inputs[0].index();
    }
    if (!suffixSlots.empty() && walkNode < colorCount && slotOf[walkNode] != SIZE_MAX) {
      rootIndex = slotOf[walkNode];
      for (size_t i = 0; i < suffixSlots.size(); ++i) {
        slots[suffixSlots[i]].in0 =
            i + 1 < suffixSlots.size() ? static_cast<int>(suffixSlots[i + 1]) : -4;
      }
      clipCoverageSlot = static_cast<int>(suffixSlots.front());
      clipChainInput = clipCoverageSlot;
    }
  }
  int coverageRootSlot = -1;
  if (coverageGraph != nullptr) {
    // Accepted roots: a single blend consuming the unit input (the xfer-dst coverage family), a
    // bare texture or gradient consuming the unit alpha, a constant (the inverted shader mask
    // filter whose shader yields no processor: ConstColor in any input mode, the same
    // pmApplyConstColor math as the runtime emission), or an analytic coverage node
    // (RectCoverage / RRectCoverage) closing a chain that starts at the unit. The
    // unit may otherwise feed only texture leaves (their input is unused — blend operands sample
    // raw), the gradient (a blend child, so it reads the opaque -4 designator), and analytic
    // coverage nodes. Analytic clip slots may multiply a blend-rooted subtree: the scalar coverage
    // commutes with the pointwise blend result, so the product matches the runtime chain order
    // (verified differentially). Two-child blends keep the plain route rather than risking a wrong
    // value when the GP coverage is below 1.0.
    const size_t covRootCombined = colorCount + coverageRoot.index();
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
    // unit input (the blend-count rule already rejects blends under an analytic root).
    if (coverageBlendCount != (rootIsBlend ? 1 : 0) || (!rootConsumesUnit && !rootIsAnalytic)) {
      return false;
    }
    auto covSlot = slotOf[covRootCombined];
    if (covSlot == SIZE_MAX) {
      return false;
    }
    coverageRootSlot = static_cast<int>(covSlot);
    if (coverageLeafFromUVCoord) {
      // The atlas glyph mask is TRUE coverage in the runtime composite (the dedicated MaskFill
      // shader carries it as coverage — see AtlasTextNonSrcOverBlendKeepsDstProbe), unlike a
      // MaskFilter's source-modulating mask, whose subtree keeps the CoverageRootIndex source
      // route. Route the atlas subtree's root through the clip-coverage register so it rides the
      // XP's coverage input with the unpremultiplied source. A draw clip then chains on top of
      // the glyph (glyph * clip), so the glyph register is the clip chain's innermost input. An
      // early-fold clip suffix sits in the color block, ahead of the glyph slots, and cannot
      // read the glyph register.
      if (clipCoverageSlot >= 0) {
        return false;
      }
      clipCoverageSlot = coverageRootSlot;
      clipChainInput = coverageRootSlot;
      coverageRootSlot = -1;
    }
  }
  // The narrow clip slots no longer multiply the color root: they chain from the scalar unit one
  // (-4, the innermost link) so their product is a pure clip coverage value the kernel reads
  // through ClipCoverageRegister and composites as the XP's coverage input on top of the GP
  // coverage — folding them into the source was only correct for SrcOver-class blending, and
  // chaining them from the coverage unit (-3) squared the GP coverage. The color root stays the
  // pre-clip result.
  auto appendClipSlot = [&](AOTChainSlot clipSlot) {
    clipSlot.in0 = clipChainInput;
    clipChainInput = static_cast<int>(slots.size());
    clipCoverageSlot = clipChainInput;
    slots.push_back(clipSlot);
  };
  for (auto* deviceRect : clipSlots.deviceRects) {
    AOTChainSlot rectSlot = {};
    rectSlot.op = AOTChainOp::AARectCoverage;
    const auto& rect = deviceRect->getRect();
    rectSlot.rectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    appendClipSlot(rectSlot);
  }
  for (auto* localRect : clipSlots.localRects) {
    AOTChainSlot localRectSlot = {};
    localRectSlot.op = AOTChainOp::LocalRectCoverage;
    const auto& rect = localRect->getRect();
    localRectSlot.localRectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& matrix = localRect->getDeviceToLocal();
    localRectSlot.localRectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2],
                                                     matrix[3], matrix[4], matrix[5],
                                                     matrix[6], matrix[7], matrix[8]};
    appendClipSlot(localRectSlot);
  }
  for (const auto& nonAARect : clipSlots.nonAARects) {
    AOTChainSlot rectSlot = {};
    rectSlot.op = AOTChainOp::RRectCoverage;
    const auto& rect = nonAARect.rect;
    rectSlot.rrectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& matrix = nonAARect.deviceToLocal;
    rectSlot.rrectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                                            matrix[5], matrix[6], matrix[7], matrix[8]};
    rectSlot.rrectCoverage.antiAlias = 0.0f;
    appendClipSlot(rectSlot);
  }
  for (auto* rrectEffect : clipSlots.rrects) {
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
    appendClipSlot(rrectSlot);
  }
  if (slots.size() > AOTPointwiseChainProcessor::MaxSlots) {
    return false;
  }
  // Register allocation: the slot wiring built above uses instruction ordinals; this pass
  // rewrites every reference into the kernel's recycled chainResults registers, so the
  // per-profile instruction budget (16 for Chain and gradient, 32 for noise) and the
  // live-result budget (MaxRegisters) are independent. A DAG exceeding the register budget is
  // refused here.
  std::vector<std::vector<int>> instructionInputs(slots.size());
  for (size_t index = 0; index < slots.size(); ++index) {
    for (int input : {slots[index].in0, slots[index].in1}) {
      if (input >= 0) {
        instructionInputs[index].push_back(input);
      }
    }
  }
  AOTChainRegisterAssignment assignment = {};
  if (!AllocateChainRegisters(instructionInputs, static_cast<int>(rootIndex), coverageRootSlot,
                              clipCoverageSlot, AOTPointwiseChainProcessor::MaxRegisters,
                              &assignment)) {
    return false;
  }
  for (size_t index = 0; index < slots.size(); ++index) {
    if (slots[index].in0 >= 0) {
      slots[index].in0 = assignment.outRegister[static_cast<size_t>(slots[index].in0)];
    }
    if (slots[index].in1 >= 0) {
      slots[index].in1 = assignment.outRegister[static_cast<size_t>(slots[index].in1)];
    }
    slots[index].outRegister = assignment.outRegister[index];
  }
  std::vector<int> tiledLeafIndices = {};
  for (const auto& tiled : layout->tiledLeaves) {
    tiledLeafIndices.push_back(tiled.leafIndex);
  }
  // The processor's own data rules and the final profile's capacities decide last, on the exact
  // instruction array construction will hand to AOTPointwiseChainProcessor::Make.
  if (assignment.rootRegister < 0 ||
      !AOTPointwiseChainProcessor::ValidateSlots(
          slots, static_cast<size_t>(assignment.rootRegister), assignment.coverageRootRegister,
          assignment.clipCoverageRegister, leafCount, layout->lutGradient != nullptr,
          tiledLeafIndices, &finalProfile)) {
    return false;
  }
  layout->slots = std::move(slots);
  layout->rootRegister = static_cast<size_t>(assignment.rootRegister);
  layout->coverageRootRegister = assignment.coverageRootRegister;
  layout->clipCoverageRegister = assignment.clipCoverageRegister;
  layout->profile = finalProfile;
  return true;
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
  if (layout.noise != nullptr) {
    for (const auto& view : {layout.noise->permutationsView, layout.noise->noiseView}) {
      if (view == nullptr || view->getContext() == nullptr) {
        return nullptr;
      }
      auto proxy = view->getContext()->proxyProvider()->wrapTextureView(view);
      if (proxy == nullptr) {
        return nullptr;
      }
      SamplingArgs lookupArgs = {TileMode::Clamp, TileMode::Clamp,
                                 SamplingOptions(FilterMode::Nearest), SrcRectConstraint::Fast};
      auto matrix = Matrix::I();
      auto child = TextureEffect::Make(allocator, proxy, lookupArgs, &matrix);
      if (child == nullptr) {
        return nullptr;
      }
      if (noisePaddingProxy == nullptr) {
        noisePaddingProxy = proxy;
      }
      samplerPadding.push_back(std::move(child));
    }
  }
  std::shared_ptr<TextureProxy> paddingProxy = nullptr;
  SamplerState paddingState = {};
  auto choosePadding = [&](const AOTEffectNode* node) {
    if (paddingProxy != nullptr || node == nullptr) {
      return;
    }
    if (const auto* texture = std::get_if<AOTTextureParameters>(&node->parameters)) {
      paddingProxy = texture->textureProxy;
      if (texture->samplingKind == AOTTextureSamplingKind::Tiled &&
          texture->tiledRecipe.has_value()) {
        paddingState = texture->tiledRecipe->hardwareSampler;
      } else {
        paddingState = texture->samplerState;
      }
    } else if (const auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters)) {
      // A LUT-gradient-only chain has no TextureSource node; its baked LUT texture serves as
      // the padding texture just as it served as the phantom before.
      if (gradient->lutProxy != nullptr) {
        paddingProxy = gradient->lutProxy;
      }
    }
  };
  for (size_t index = 0; index < graph.nodeCount(); ++index) {
    AOTNodeID nodeID(static_cast<uint32_t>(index));
    if (layout.noise != nullptr &&
        std::find(pass.nodes.begin(), pass.nodes.end(), nodeID) == pass.nodes.end()) {
      continue;
    }
    choosePadding(graph.nodeAt(nodeID));
  }
  for (size_t index = 0; coverageGraph != nullptr && index < coverageGraph->nodeCount(); ++index) {
    choosePadding(coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index))));
  }
  if (paddingProxy == nullptr && noisePaddingProxy != nullptr) {
    paddingProxy = noisePaddingProxy;
    paddingState = SamplerState(SamplingOptions(FilterMode::Nearest));
  }
  SamplingOptions paddingSampling(paddingState.minFilterMode, paddingState.magFilterMode,
                                  paddingState.mipmapMode);
  SamplingArgs paddingArgs = {paddingState.tileModeX, paddingState.tileModeY, paddingSampling,
                              SrcRectConstraint::Fast};
  size_t samplerChildren = leaves.size() + (lutChild != nullptr ? 1 : 0) + samplerPadding.size();
  if (samplerChildren > 0 && samplerChildren < 4) {
    if (paddingProxy == nullptr) {
      return nullptr;
    }
    while (samplerChildren < 4) {
      auto phantomMatrix = Matrix::I();
      auto phantom = TextureEffect::Make(allocator, paddingProxy, paddingArgs, &phantomMatrix);
      if (phantom == nullptr) {
        return nullptr;
      }
      samplerPadding.push_back(std::move(phantom));
      ++samplerChildren;
    }
  }
  PlacementPtr<FragmentProcessor> maskChild = nullptr;
  if (maskEffect != nullptr) {
    // The mask child's Subset write routes to the kernel's DeviceMaskSubset decoy, so a leaf's
    // real subset rect survives in the shared uniform block.
    maskChild = DeviceSpaceTextureEffect::Make(allocator, maskEffect->getTextureProxy(),
                                               maskEffect->getUVMatrix());
    if (maskChild == nullptr) {
      return nullptr;
    }
  }
  // Four-leaf artifacts declare the mask sampler unconditionally, so a mask-free chain binds a
  // phantom there (re-using the same padding texture) and clears HasMaskTexture at runtime.
  bool maskChildIsPhantom = false;
  if (samplerChildren == 4 && maskChild == nullptr) {
    if (paddingProxy == nullptr) {
      return nullptr;
    }
    auto phantomMatrix = Matrix::I();
    maskChild = TextureEffect::Make(allocator, paddingProxy, paddingArgs, &phantomMatrix);
    if (maskChild == nullptr) {
      return nullptr;
    }
    maskChildIsPhantom = true;
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
      std::move(maskChild), layout.coverageRootRegister, layout.coordSourceMask,
      std::move(lutChild), lutLeafIndex, std::move(samplerPadding), maskChildIsPhantom,
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

}  // namespace tgfx
