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
#include "core/utils/Log.h"
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
 * bucket. content.image is usually populated, but a bucket kept alive by an own-outline style whose
 * content render failed leaves it null; contour is only present when at least one Contour-sourced
 * LayerStyle in this bucket requires a distinct contour image.
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
 * Returns whether the style declares the layer's own geometry as an input.
 */
inline bool NeedsOwnShape(const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  return (style->extraSourceType() & static_cast<uint32_t>(LayerStyleExtraSourceType::OwnShape)) !=
         0;
}

/**
 * Returns the layer's own geometry entry when the given style declares it, or nullptr otherwise.
 * The single source of the own-geometry predicate shared by the resolvers below, so they can never
 * disagree on when a style is driven by the own geometry.
 */
inline const LayerStyleSourceEntry* OwnShapeEntry(const LayerStyleSourceGroup& group,
                                                  const LayerStyle* style) {
  if (NeedsOwnShape(style) && group.ownContour.has_value() && group.ownContour->image != nullptr) {
    return &*group.ownContour;
  }
  return nullptr;
}

/**
 * Resolves the entry a consumer must use as LayerStyleInput.content for the given style. A style
 * that declares LayerStyleExtraSourceType::OwnShape samples the layer's own geometry, which is the
 * only image that includes the silhouette-only markers, and falls back to the shared subtree
 * recordings when no own geometry exists (see the fallback block below); every other style keeps
 * the shared content image.
 *
 * Returns nullptr when the resolved entry carries no image: the group then has nothing to offer
 * this style, and consumers must skip it rather than draw from a missing image. style must not be
 * null. Only a style that declares the own geometry may be driven by it; handing it to a
 * content-based style would make that style draw an outline instead of the layer content.
 */
inline const LayerStyleSourceEntry* ResolveContentEntry(const LayerStyleSourceGroup& group,
                                                        const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (NeedsOwnShape(style)) {
    if (auto* ownEntry = OwnShapeEntry(group, style)) {
      return ownEntry;
    }
    // A style that declares the own geometry but records none resolves to the shared subtree contour
    // when present (and falls back to `content` otherwise).
    // A style whose geometry lives in child layers records no own geometry, and its two subtree
    // recordings then disagree on two axes. `content` (drawDefault, DrawContent.cpp) drops every
    // painter whose effective color alpha is <= 0; `contour` (drawContourInternal) has no alpha gate
    // and keeps it. In the opposite direction `contour` drops every painter flagged silhouetteOnly
    // (LayerContent.h); `content` keeps it. A retained 0%-opacity fill therefore survives only
    // in `contour`, and falling back to `content` silently removed the glass coverage over that fill
    // -- the bug this fallback fixes. The two axes have opposite signs, so the change is not
    // scope-neutral: both recordings span the same subtree (self + descendants), so preferring
    // `contour` can never draw outside that subtree, but it CAN drop coverage over a descendant's
    // silhouette-only painter that `content` still holds. That case is bounded where the style and
    // its coverage geometry live in the same layer: such a layer records an own geometry and never
    // reaches this branch. No new draw site is introduced either: the branch is only reached with
    // `ownContour` absent and `contour.image != nullptr`, and a null `content` image has already
    // marked the bucket unusable and skipped it (Layer.cpp), so the old code had already drawn -- a
    // null-content bucket cannot become a drawing one.
    if (group.contour.has_value() && group.contour->image != nullptr) {
      return &*group.contour;
    }
  }
  return group.content.image != nullptr ? &group.content : nullptr;
}

/**
 * Resolves the vector shape of the layer's own geometry for a style that declares it. The shape
 * describes the same recording as the raster ResolveContentEntry returns: when the own entry
 * exists, both come from the own geometry; when it does not, the shape stays absent and the style
 * falls back to its alpha-mask path over the shared raster.
 */
inline std::optional<StyledShape> ResolveOwnShape(const LayerStyleSourceGroup& group,
                                                  const LayerStyle* style) {
  if (OwnShapeEntry(group, style) == nullptr) {
    return std::nullopt;
  }
  return group.ownContourShape;
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
  // A bucket that recorded nothing usable is left null while the source itself stays non-null and
  // the other bucket may still be populated, so every reader must test its group before using it.
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
