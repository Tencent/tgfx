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
//  License is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/FamilyMatch.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/contract/DrawFacts.h"

namespace tgfx {

namespace {

/// The SolidColor family's rule, carried over from the former SolidColorFillShader: the
/// DefaultGP's rect fill with no fragment processor, or one direct analytic clip (an AA rect or
/// rounded rect) riding the runtime clip contract. Any xfer, alpha-only targets included.
/// Slot layout: Frame(0), RectGeometry(1), AnalyticClip(2), Xfer(3: HAS_XP). No free dims in the
/// geometry or the body, so the ordinal is HAS_XP alone.
bool MatchSolidColor(const DrawShape& shape, Variant* variant) {
  if (shape.geometry.kind != GeometryKind::Default) {
    return false;
  }
  const bool noProcessorsOrDirectClip =
      shape.fragmentCount == 0 || (shape.colorFPCount == 0 && (shape.coverage == Coverage::AARect ||
                                                               shape.coverage == Coverage::RRect));
  if (!noProcessorsOrDirectClip || shape.xpType() < XP_NONE) {
    return false;
  }
  variant->family = 0;
  variant->geometry = 0;
  variant->values[0] = static_cast<uint8_t>(shape.xpType());
  variant->ordinal = static_cast<uint32_t>(shape.xpType());
  return true;
}

/// The MaskFill family's rule, carried over from the former MaskFillShader: the DefaultGP's rect
/// fill whose sole fragment processor is the rasterized alpha-only shape mask. The mask's
/// sampler facts come off the IR's sole coverage term, so the acceptance reads the carried IR.
/// Slot layout: Frame(0), RectGeometry(1), MaskTexture(2), Xfer(3: HAS_XP).
bool MatchMaskFill(const DrawShape& shape, Variant* variant, const AOTTextureParameters** maskOut) {
  if (shape.geometry.kind != GeometryKind::Default) {
    return false;
  }
  if (shape.colorFPCount != 0 || shape.fragmentCount != 1) {
    return false;
  }
  const auto* mask = shape.ir != nullptr && shape.ir->coverage.size() == 1
                         ? BareTextureCoverageLeaf(shape.ir->coverage[0])
                         : nullptr;
  if (mask == nullptr || mask->samplingKind != AOTTextureSamplingKind::Plain ||
      !mask->isAlphaOnly || mask->subset.has_value() || mask->isYUV || mask->hasRGBAAA ||
      mask->textureProxy == nullptr || mask->textureProxy->getTextureView() == nullptr) {
    return false;
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  variant->family = 1;
  variant->geometry = 0;
  variant->values[0] = static_cast<uint8_t>(shape.xpType());
  variant->ordinal = static_cast<uint32_t>(shape.xpType());
  *maskOut = mask;
  return true;
}

/// The TiledTextureFill family's rule, carried over from the former TiledTextureFillShader: a
/// DefaultGP rect whose sole color processor is the IR's tiled texture leaf (no operators above
/// it). QuadPerEdgeAA stays with the legacy QuadTextureFillShader (the old table consulted it
/// first); the quad form migrates with that family. FULL and PRESENT are pinned to 1, so the
/// ordinal is HAS_XP alone.
bool MatchTiledTextureFill(const DrawShape& shape, Variant* variant,
                           const AOTTextureParameters** leafOut) {
  if (shape.geometry.kind != GeometryKind::Default) {
    return false;
  }
  if (shape.fragmentCount != 1 || shape.xpType() < XP_NONE || shape.ir == nullptr) {
    return false;
  }
  AOTChainBuilder::AOTTextureTailShape tail = {};
  if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty()) {
    return false;
  }
  const auto* leaf = tail.leaf;
  if (leaf->samplingKind != AOTTextureSamplingKind::Tiled || leaf->hasPerspective ||
      !leaf->tiledRecipe.has_value()) {
    return false;
  }
  if (!TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeX)) ||
      !TiledFillModeSupported(static_cast<int>(leaf->tiledRecipe->shaderModeY))) {
    return false;
  }
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  variant->family = 2;
  variant->geometry = 0;
  variant->values[0] = hasXp;
  variant->ordinal = static_cast<uint32_t>(hasXp);
  *leafOut = leaf;
  return true;
}

/// The RRect family's rule, carried over from the former NonAARRectFillShader: the non-AA
/// rounded rect, at most one fragment (a sole analytic clip riding the runtime contract, or the
/// single-tap tiled fill), never alpha-only, any xfer.
bool MatchRRect(const DrawShape& shape, Variant* variant, const AOTTextureParameters** leaf) {
  if (shape.geometry.kind != GeometryKind::NonAARRect || shape.alphaOnlyTarget) {
    return false;
  }
  if (shape.fragmentCount > 1) {
    return false;
  }
  const bool clipOnly = shape.colorFPCount == 0 &&
                        (shape.coverage == Coverage::AARect || shape.coverage == Coverage::RRect);
  const AOTTextureParameters* tiledLeaf = nullptr;
  if (shape.fragmentCount == 1 && !clipOnly) {
    if (shape.ir == nullptr) {
      return false;
    }
    AOTChainBuilder::AOTTextureTailShape tail = {};
    if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || !tail.ops.empty()) {
      return false;
    }
    tiledLeaf = tail.leaf;
    if (tiledLeaf->samplingKind != AOTTextureSamplingKind::Tiled || tiledLeaf->hasPerspective ||
        !tiledLeaf->tiledRecipe.has_value()) {
      return false;
    }
    if (!TiledModeSupported(static_cast<int>(tiledLeaf->tiledRecipe->shaderModeX)) ||
        !TiledModeSupported(static_cast<int>(tiledLeaf->tiledRecipe->shaderModeY))) {
      return false;
    }
    // The textured form never combines with stroke (the family's allowed rule).
    if (shape.geometry.isStroke) {
      return false;
    }
  }
  if (shape.xpType() < XP_NONE) {
    return false;
  }
  // Slot layout: Frame(0), RRectGeometry(1: STROKE), TiledTexture(2: FULL pinned, PRESENT),
  // AnalyticClip(3), Xfer(4: HAS_XP). The ordinal encodes [STROKE, PRESENT, HAS_XP].
  const uint8_t stroke = shape.geometry.isStroke ? 1 : 0;
  const uint8_t present = tiledLeaf != nullptr ? 1 : 0;
  const uint8_t hasXp = static_cast<uint8_t>(shape.xpType());
  variant->family = 3;
  variant->geometry = 0;
  variant->values[0] = stroke;
  variant->values[1] = 0;
  variant->values[2] = present;
  variant->values[3] = hasXp;
  variant->ordinal = static_cast<uint32_t>(stroke) + 2u * present + 4u * hasXp;
  *leaf = tiledLeaf;
  return true;
}

}  // namespace

std::optional<FamilyMatchResult> MatchAssembledFamily(const DrawShape& shape) {
  if (FamilyCount() == 0) {
    return std::nullopt;
  }
  // The table order is the match priority; the dedicated rules are disjoint.
  FamilyMatchResult result = {};
  if (MatchSolidColor(shape, &result.variant)) {
    return result;
  }
  if (MatchMaskFill(shape, &result.variant, &result.maskLeaf)) {
    return result;
  }
  if (MatchTiledTextureFill(shape, &result.variant, &result.tiledLeaf)) {
    return result;
  }
  if (MatchRRect(shape, &result.variant, &result.tiledLeaf)) {
    return result;
  }
  return std::nullopt;
}

}  // namespace tgfx
