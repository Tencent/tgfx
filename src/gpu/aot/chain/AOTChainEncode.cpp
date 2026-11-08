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

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/chain/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node,
                        AOTChainSlot* slot) {
  if (node == nullptr || slot == nullptr) {
    return false;
  }
  if (node->kind == AOTEffectKind::ConstColor) {
    auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
    if (parameters == nullptr || node->inputs.size() != 1) {
      return false;
    }
    slot->op = AOTChainOp::ConstColor;
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
    slot->op = AOTChainOp::Blend;
    slot->blend = *parameters;
    slot->constColor = *constParams;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorMatrix) {
    auto parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->op = AOTChainOp::ColorMatrix;
    slot->colorMatrix = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::Luma) {
    auto parameters = std::get_if<ChainISA::LumaData>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->op = AOTChainOp::Luma;
    slot->luma = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::AlphaThreshold) {
    auto parameters = std::get_if<ChainISA::AlphaThresholdData>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->op = AOTChainOp::AlphaThreshold;
    slot->alphaThreshold = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorSpaceXform) {
    auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
    if (parameters == nullptr || parameters->steps == nullptr) {
      return false;
    }
    slot->op = AOTChainOp::ColorSpaceXform;
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
  auto designator = static_cast<int>(TraitsOf(node->kind).designator);
  if (designator != 0) {
    return designator;
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

// Maps one op input edge onto its chain register/designator, in the input's own node space.
static int MapChainOpInput(const AOTChainContext* ctx, size_t inputBase, AOTNodeID input) {
  return MapChainInput(ctx->nodes, ctx->slotOf, inputBase + input.index());
}

// Stage three's per-kind encoders: each turns one scheduled node into its chain slot and
// registers the layout side data it carries (texture leaves, the LUT gradient, the noise source).
// The contract rows live in NodeContract.cpp, indexed by AOTEffectKind and pinned to the enum's
// size, so adding a kind means writing one encoder and one row — a missing row fails the build. A
// null row is a kind that never schedules: the graph roots and the input designators stay out of
// the ordered set. The encoder bodies stay with the pipeline that owns the context, so they sit
// outside the anonymous namespace (declared in ChainContext.h).

bool EncodeTextureSourceSlot(AOTChainContext* ctx, size_t combined, size_t index) {
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

bool EncodePerlinNoiseSlot(AOTChainContext* ctx, size_t combined, size_t index) {
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

bool EncodeConstColorSlot(AOTChainContext* ctx, size_t combined, size_t index) {
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

bool EncodeUnaryPointwiseSlot(AOTChainContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  if (node->inputs.size() != 1) {
    return false;
  }
  if (!BuildPointwiseSlot(*ctx->graph, node, &slot)) {
    return false;
  }
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

bool EncodeInputOpaqueSlot(AOTChainContext* ctx, size_t combined, size_t index) {
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

bool EncodeMulAlphaSlot(AOTChainContext* ctx, size_t combined, size_t index) {
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

bool EncodeBlendSlot(AOTChainContext* ctx, size_t combined, size_t index) {
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

bool EncodeRectCoverageSlot(AOTChainContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  // Every coverage slot owns its own data-pool block, so the only limits are the slot and
  // register budgets the whole chain shares.
  if (IsIdentityMatrix(parameters->deviceToLocal)) {
    slot.op = AOTChainOp::AARectCoverage;
    slot.rectCoverage = *parameters;
  } else {
    slot.op = AOTChainOp::LocalRectCoverage;
    slot.localRectCoverage = *parameters;
  }
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

bool EncodeRRectCoverageSlot(AOTChainContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  slot.op = AOTChainOp::RRectCoverage;
  slot.rrectCoverage = *parameters;
  slot.in0 = MapChainOpInput(ctx, inputBase, node->inputs[0]);
  return true;
}

bool EncodeGradientSlot(AOTChainContext* ctx, size_t combined, size_t index) {
  auto node = ctx->nodes[combined];
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  auto* layout = ctx->layout;
  auto& slot = ctx->slots[index];
  auto parameters = std::get_if<AOTGradientParameters>(&node->parameters);
  if (parameters == nullptr || node->inputs.size() != 1) {
    return false;
  }
  // Single-authority budget: MaxChainGradientSlots (the kernel
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

// Stage four's per-kind resource demand: a kind that consumes kernel resources adds its demand,
// every other kind adds none. The noise demand is counted from the layout's noise record instead
// (the layout owns it after stage three), so its row is null.
void DemandTextureSource(const AOTEffectNode* node, AOTResourceDemand* demand) {
  auto* texture = std::get_if<AOTTextureParameters>(&node->parameters);
  if (texture == nullptr) {
    return;
  }
  ++demand->leafBindings;
  if (HasShaderTiling(*texture)) {
    ++demand->tiledRecipes;
  }
}

void DemandGradientSource(const AOTEffectNode* node, AOTResourceDemand* demand) {
  auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters);
  if (gradient == nullptr) {
    return;
  }
  ++demand->gradients;
  if (gradient->colorizerKind == 3) {
    ++demand->leafBindings;
  }
}

// Stage three: encodes every scheduled node into its chain slot (the per-kind op, its parameter
// block, and its mapped input registers), collecting the texture leaves, the LUT gradient and the
// noise source into the layout along the way. Per-kind budgets that need no cross-node state are
// enforced by the encoders (the rect/rrect coverage capacities, the single gradient).
bool EncodeChainSlots(AOTChainContext* ctx) {
  auto* layout = ctx->layout;
  ctx->slots.assign(ctx->ordered.size(), AOTChainSlot{});
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
    auto encoder = NodeContractOf(node->kind).encode;
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

}  // namespace tgfx
