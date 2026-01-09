/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/ImageFilter.h"
#include "tgfx/layers/LayerProperty.h"

namespace tgfx {
/**
 * LayerFilter represents a filter that applies effects to a layer, such as blurs, shadows, or color
 * adjustments. It creates a new offscreen image that replaces the original layer content.
 * LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Returns the current image filter for the given scale factor. If the filter has not been
   * created yet, it will be created and cached.
   * @param scale The scale factor to apply to the filter.
   * @return The current image filter.
   */
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

  /**
   * Returns the bounds of the layer filter after applying it to the scaled layer bounds.
   * @param srcRect The scaled bounds of the layer content.
   * @param contentScale The scale factor of the layer bounds relative to its original size.
   * Some layer filters have size-related parameters that must be adjusted with this scale factor.
   * @return The bounds of the layer filter.
   */
  Rect filterBounds(const Rect& srcRect, float contentScale);

 protected:
  enum class Type {
    LayerFilter,
    BlendFilter,
    BlurFilter,
    ColorMatrixFilter,
    DropShadowFilter,
    InnerShadowFilter
  };

  virtual Type type() const {
    return Type::LayerFilter;
  }
  /**
   * Creates a new image filter for the given scale factor. When it is necessary to recreate the
   * ImageFilter, the onCreateImageFilter method will be called.
   * @param scale The scale factor to apply to the filter.
   * @return A new image filter.
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) = 0;

  /**
   * Marks the filter as dirty and invalidates the cached filter.
   */
  void invalidateFilter();

 private:
  bool dirty = true;
  float lastScale = 1.0f;
  std::unique_ptr<Rect> _clipBounds = nullptr;
  std::shared_ptr<ImageFilter> lastFilter;

  friend class Types;
};
}  // namespace tgfx
