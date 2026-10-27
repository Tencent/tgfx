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
#include "tgfx/layers/layerstyles/LayerStyleInput.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

/**
 * Holds the recordings one excludeChildEffects bucket resolves its styles from. Each is built once.
 * content is null unless some style in the bucket draws from it, contour unless a Contour style
 * needs one or a geometry-less bucket falls back.
 */
struct LayerStyleSourceGroup {
  // The shared content of this bucket, as the source a style resolves.
  std::shared_ptr<StyleInputSource> content = nullptr;

  // The layer's geometry (LayerStyleContentSource::Geometry), null unless a style needs it. It is
  // recorded once per layer and shared by both buckets, because that recording never reads
  // excludeEffects. Only this source carries a shape, read back from that same recording.
  std::shared_ptr<StyleInputSource> geometry = nullptr;

  // The subtree contour standing in for a missing geometry; carries no shape, because it is this
  // bucket's own raster, which no exact outline describes.
  std::shared_ptr<StyleInputSource> contour = nullptr;
};

/**
 * Returns whether the style declares the layer's geometry as its content.
 */
inline bool NeedsGeometry(const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  return style->contentSource() == LayerStyleContentSource::Geometry;
}

/**
 * Returns the source a consumer must use as LayerStyleInput.content: a style declaring
 * LayerStyleContentSource::Geometry gets the layer's geometry, or the bucket's contour when the
 * layer records no geometry of its own; every other style keeps the shared content.
 *
 * Returns nullptr when the style has nothing recorded to draw, and the caller must then skip it.
 */
inline std::shared_ptr<StyleInputSource> ResolveContentSource(const LayerStyleSourceGroup& group,
                                                              const LayerStyle* style) {
  DEBUG_ASSERT(style != nullptr);
  if (NeedsGeometry(style)) {
    if (group.geometry != nullptr) {
      return group.geometry;
    }
    // Reached when the geometry recording is empty, e.g. it all lives in child layers. The shared
    // contour answers for it because a retained 0%-alpha fill survives only there: `content` drops
    // painters with alpha <= 0, `contour` keeps them.
    return group.contour;
  }
  return group.content;
}

/**
 * Shared drawing inputs for all LayerStyles attached to a Layer. groups[0] covers styles whose
 * excludeChildEffects is false (they see children, filters, etc.); groups[1] covers styles whose
 * excludeChildEffects is true: it drops the child layers' styles and filters, while their plain
 * content is still drawn. contentScale is the scale factor mapping layer-local bounds to the
 * captured images.
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
