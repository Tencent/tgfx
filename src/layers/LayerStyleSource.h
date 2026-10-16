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
 * Returns the layer's own outline entry when the given style draws from it, or nullptr otherwise.
 * The single source of the own-outline predicate shared by the resolvers below, so they can never
 * disagree on when a style is driven by the own outline.
 */
inline const LayerStyleSourceEntry* OwnContourEntry(const LayerStyleSourceGroup& group,
                                                    const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (style->usesOwnOutlineAsContent() && group.ownContour.has_value() &&
      group.ownContour->image != nullptr) {
    return &*group.ownContour;
  }
  return nullptr;
}

/**
 * Resolves the entry a consumer must use as LayerStyleInput.content for the given style. A style that
 * declares usesOwnOutlineAsContent() wants the layer's own outline, which is the only image that
 * includes the silhouette-only markers, and falls back to the shared subtree recordings when no own
 * outline exists (see the fallback block below); every other style keeps the shared content image.
 *
 * Returns nullptr when the resolved entry carries no image: the group then has nothing to offer
 * this style, and consumers must skip it rather than draw from a missing image. style must not be
 * null. Only a style that declares the own outline may be driven by it; handing it to a content-based
 * style would make that style draw an outline instead of the layer content.
 */
inline const LayerStyleSourceEntry* ResolveContentEntry(const LayerStyleSourceGroup& group,
                                                        const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (style->usesOwnOutlineAsContent()) {
    if (auto* ownEntry = OwnContourEntry(group, style)) {
      return ownEntry;
    }
    // A style that declares the own outline but records none resolves to the shared subtree contour
    // when present (and falls back to `content` otherwise).
    // A style whose geometry lives in child layers records no own outline, and its two subtree
    // recordings then disagree on two axes. `content` (drawDefault, DrawContent.cpp) drops every
    // painter whose effective color alpha is <= 0; `contour` (drawContourInternal) has no alpha gate
    // and keeps it. In the opposite direction `contour` drops every painter flagged silhouetteOnly
    // (LayerContent.h); `content` keeps it. A retained 0%-opacity fill therefore survives only
    // in `contour`, and falling back to `content` silently removed the glass coverage over that fill
    // -- the bug this fallback fixes. The two axes have opposite signs, so the change is not
    // scope-neutral: both recordings span the same subtree (self + descendants), so preferring
    // `contour` can never draw outside that subtree, but it CAN drop coverage over a descendant's
    // silhouette-only painter that `content` still holds. That case is bounded where the style and
    // its coverage geometry live in the same layer: such a layer records an own outline and never
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
 * Resolves the shape that describes the image ResolveContentEntry hands to the style. The shape
 * always comes from the same recording as that image: a style that declares the own outline and
 * finds one gets the shape recorded with it, which may itself be absent -- an absent own shape stays
 * absent rather than being replaced by the shared shape, because the shared shape describes a
 * different recording. Every other case, including the fallback where such a style is handed the
 * shared contour raster, gets the shared content shape.
 *
 * The pairing matters because the style turns this shape into a path (DetectGlassShape) while it
 * samples the image as coverage, so a shape that disagreed with the raster would treat a different
 * region as the covered one. In the fallback, the shape and the raster are two separate recordings
 * that differ on the alpha <= 0 and silhouetteOnly axes (see ResolveContentEntry above), so the
 * pairing holds only under an invariant the code relies on rather than enforces: a group that
 * records no own outline has no shape of its own either, so the shared shape handed out here is
 * absent as well. That holds today for the container-like layer this fallback serves, whose geometry
 * lives in child layers and whose own content therefore contributes no shape; a group that recorded
 * no own outline while still owning a shape would hand a shape that need not match the raster.
 */
inline const std::optional<StyledShape>& ResolveContourShape(
    const LayerStyleSourceGroup& group, const LayerStyle* style,
    const std::optional<StyledShape>& sharedShape) {
  DEBUG_ASSERT(style != nullptr);
  if (OwnContourEntry(group, style) != nullptr) {
    return group.ownContourShape;
  }
  return sharedShape;
}

/**
 * Resolves the entry whose raster pairs with the shape ResolveContourShape returns: the own-outline
 * entry when the style draws from it, otherwise the shared contour entry when it carries an image,
 * otherwise nullptr. Image, offset and shape must all come from one recording, so a style handed the
 * own-outline shape must also be handed the own-outline raster.
 */
inline const LayerStyleSourceEntry* ResolveContourEntry(const LayerStyleSourceGroup& group,
                                                        const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (auto* ownEntry = OwnContourEntry(group, style)) {
    return ownEntry;
  }
  if (group.contour.has_value() && group.contour->image != nullptr) {
    return &*group.contour;
  }
  return nullptr;
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
