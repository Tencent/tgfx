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

#pragma once

#include <memory>
#include <optional>
#include "core/utils/MathExtra.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

/**
 * Cached per-source image plus its offset relative to the layer's local coordinate system.
 * Produced once per LayerStyleSource group and consumed by every LayerStyle that matches the
 * group's excludeChildEffects bucket.
 */
struct LayerStyleSourceEntry {
  std::shared_ptr<Image> image = nullptr;
  Point offset = {};
};

/**
 * Pairs the content image with the optional contour image for a specific excludeChildEffects
 * bucket. content.image is usually populated, but a Glass bucket whose content render failed is
 * kept alive by its own outline and then leaves content.image null; contour is only present when at
 * least one Contour-sourced LayerStyle in this bucket requires a distinct contour image.
 */
struct LayerStyleSourceGroup {
  LayerStyleSourceEntry content = {};
  std::optional<LayerStyleSourceEntry> contour = std::nullopt;

  // The layer's own outline: its content without descendants, but including the painters marked
  // silhouetteOnly that the shared contour deliberately drops. Recorded when the bucket carries a
  // style that consumes it, so a layer whose only painter is such a marker still has coverage.
  std::optional<LayerStyleSourceEntry> ownContour = std::nullopt;

  // Shape of the same own outline, matching ownContour the way contentShape matches content.
  std::optional<StyledShape> ownContourShape = std::nullopt;
};

/**
 * Resolves the entry a consumer must use as LayerStyleInput.content for the given style. Glass wants
 * the layer's own outline, which is the only image that includes the silhouette-only markers, and
 * falls back to the shared subtree recordings when no own outline exists (see the fallback block
 * below); every other style keeps the shared content image.
 *
 * Returns nullptr when the resolved entry carries no image: the group then has nothing to offer
 * this style, and consumers must skip it rather than draw from a missing image. Only Glass may be
 * driven by the own outline; handing it to a content-based style would make that style draw an
 * outline instead of the layer content.
 */
inline const LayerStyleSourceEntry* ResolveContentEntry(const LayerStyleSourceGroup& group,
                                                        LayerStyleType styleType) {
  if (styleType == LayerStyleType::Glass) {
    if (group.ownContour.has_value() && group.ownContour->image != nullptr) {
      return &*group.ownContour;
    }
    // Glass that records no own outline resolves to the shared subtree contour when present (and
    // falls back to `content` otherwise).
    // A Glass whose geometry lives in child layers records no own outline, and its two subtree
    // recordings then disagree on two axes. `content` (drawDefault, DrawContent.cpp) drops every
    // painter whose effective color alpha is <= 0; `contour` (drawContourInternal) has no alpha gate
    // and keeps it. In the opposite direction `contour` drops every painter flagged silhouetteOnly
    // (LayerContent.h); `content` keeps it. A retained 0%-opacity user fill therefore survives only
    // in `contour`, and falling back to `content` silently removed the glass coverage over that fill
    // -- the bug this fallback fixes. The two axes have opposite signs, so the change is not
    // scope-neutral: both recordings span the same subtree (self + descendants), so preferring
    // `contour` can never draw outside that subtree, but it CAN drop coverage over a descendant's
    // silhouette-only painter that `content` still holds. That case is bounded in the editor flow,
    // where the coverage marker is injected into the glass node's own content, so the node always
    // records an own outline and never reaches this branch. No new draw site is introduced either:
    // the branch is only reached with `ownContour` absent and `contour.image != nullptr`, and a null
    // `content` image has already marked the bucket unusable and skipped it (Layer.cpp), so the old
    // code had already drawn -- a null-content bucket cannot become a drawing one.
    if (group.contour.has_value() && group.contour->image != nullptr) {
      return &*group.contour;
    }
  }
  return group.content.image != nullptr ? &group.content : nullptr;
}

/**
 * Resolves the shape that describes the contour image handed to the style: Glass matches its own
 * outline, every other style keeps the shared content shape. Once a group carries an own outline the
 * shape must come from that outline too — even when it is absent — so the shape and the own outline
 * stay paired; only a group without an own outline falls back to the shared shape.
 *
 * The shape must stay paired with the content image ResolveContentEntry returns, because Glass turns
 * it into the SDF path in DetectGlassShape and a shape that disagreed with the raster would refract
 * a different region than the one that is covered. The fallback above resolves Glass to the
 * shared contour raster whenever no own outline exists, and this function deliberately keeps
 * returning the shared content shape for that same case. The shared shape and the contour raster are
 * not guaranteed pixel-exact -- they are two separate recordings that differ on the alpha <= 0 and
 * silhouetteOnly axes (see ResolveContentEntry above) -- but the mismatch cannot manifest in the flow
 * this fallback serves: a group that records no own outline has no own content shape either, so it
 * hands no shape here, exactly as it did before this fallback, and the image and shape stay paired by
 * both being absent.
 */
inline const std::optional<StyledShape>& ResolveContourShape(
    const LayerStyleSourceGroup& group, LayerStyleType styleType,
    const std::optional<StyledShape>& sharedShape) {
  if (styleType == LayerStyleType::Glass && group.ownContour.has_value() &&
      group.ownContour->image != nullptr) {
    return group.ownContourShape;
  }
  return sharedShape;
}

/**
 * Shared drawing inputs for all LayerStyles attached to a Layer. groups[0] covers styles whose
 * excludeChildEffects is false (they see children, filters, etc.); groups[1] covers styles whose
 * excludeChildEffects is true (they see only the layer's own content). contentScale is the scale
 * factor mapping layer-local bounds to the captured images.
 */
struct LayerStyleSource {
  float contentScale = 1.0f;

  // groups[0]: excludeChildEffects = false
  // groups[1]: excludeChildEffects = true
  std::unique_ptr<LayerStyleSourceGroup> groups[2] = {};

  // Simplified content shape of the layer for LayerStyles that need vector access (e.g. shadow
  // spread). std::nullopt when no LayerStyle needs it.
  std::optional<StyledShape> contentShape = std::nullopt;
};

/**
 * Turns a recorded Picture into an Image whose origin is placed at imageBounds (or the picture's
 * own bounds when imageBounds is null). offset receives the resulting top-left corner in the
 * source coordinate system so callers can align consumers. roundOutBounds should be true for
 * offscreen compositing (where the canvas matrix has already been baked into the picture) and
 * false for caching scenarios (where the canvas matrix is applied separately at draw time).
 */
inline std::shared_ptr<Image> ToImageWithOffset(
    std::shared_ptr<Picture> picture, Point* offset, const Rect* imageBounds = nullptr,
    std::shared_ptr<ColorSpace> colorSpace = ColorSpace::SRGB(), bool roundOutBounds = true) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = imageBounds ? *imageBounds : picture->getBounds();
  if (roundOutBounds) {
    // In off-screen rendering scenarios, the canvas matrix is applied to the picture, requiring
    // bounds to be rounded out to keep offsets integral and avoid redundant sampling.
    // During caching, the canvas matrix is not applied to the picture, so rounding is unnecessary.
    bounds.roundOut();
  }
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), FloatCeilToInt(bounds.width()),
                               FloatCeilToInt(bounds.height()), &matrix, std::move(colorSpace));
  if (offset) {
    offset->x = bounds.left;
    offset->y = bounds.top;
  }
  return image;
}

}  // namespace tgfx
