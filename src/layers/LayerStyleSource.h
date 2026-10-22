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
 * bucket. content.image is populated only when some style in the bucket draws from the shared
 * content; a bucket whose styles all declare LayerStyleContentSource::Geometry leaves it null, as
 * does a bucket kept alive by such a style whose layer paints nothing. contour is only present when
 * at least one Contour-sourced LayerStyle in this bucket requires a distinct contour image, or when
 * it holds the lazy fallback recording of a bucket whose own geometry is empty.
 */
struct LayerStyleSourceGroup {
  LayerStyleSourceEntry content = {};
  std::optional<LayerStyleSourceEntry> contour = std::nullopt;

  // The layer's own geometry: its content recorded without descendants, keeping the painters with
  // alpha=0, plus the geometries no painter captured. Recorded when the bucket carries a style whose
  // contentSource() is Geometry, so a layer that paints nothing still has coverage.
  std::optional<LayerStyleSourceEntry> ownGeometry = std::nullopt;

  // Shape of the same own geometry, matching ownGeometry the way contentShape matches content.
  std::optional<StyledShape> ownShape = std::nullopt;
};

/**
 * Returns whether the style declares the layer's own geometry as its content.
 */
inline bool NeedsOwnGeometry(const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  return style->contentSource() == LayerStyleContentSource::Geometry;
}

/**
 * Resolves the entry a consumer must use as LayerStyleInput.content: a style declaring
 * LayerStyleContentSource::Geometry gets the layer's own geometry (with the fallbacks described
 * there), every other style keeps the shared content.
 *
 * Returns nullptr when the entry carries no image, and the caller must then skip the style.
 */
inline const LayerStyleSourceEntry* ResolveContentEntry(const LayerStyleSourceGroup& group,
                                                        const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (NeedsOwnGeometry(style)) {
    if (group.ownGeometry.has_value() && group.ownGeometry->image != nullptr) {
      return &*group.ownGeometry;
    }
    // Only reached when the own recording is empty, e.g. the geometry all lives in child layers.
    // The shared contour answers for it because a retained 0%-alpha fill survives only there:
    // `content` drops painters with alpha <= 0, `contour` keeps them.
    if (group.contour.has_value() && group.contour->image != nullptr) {
      return &*group.contour;
    }
    return nullptr;
  }
  return group.content.image != nullptr ? &group.content : nullptr;
}

/**
 * Resolves the own-geometry shape handed to a style. Only a style that declares
 * LayerStyleContentSource::Geometry consumes the layer's own geometry, and a group may carry the
 * shape for a sibling in the same excludeChildEffects bucket, so every other style must receive an
 * empty shape: its content is the shared recording, and pairing it with an own shape that does not
 * describe that content would make it read bounds it never drew.
 */
inline std::optional<StyledShape> ResolveOwnShape(const LayerStyleSourceGroup& group,
                                                  const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (!NeedsOwnGeometry(style)) {
    return std::nullopt;
  }
  return group.ownShape;
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
