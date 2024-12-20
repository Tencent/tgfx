/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
 * adjustments. LayerFilters are mutable and can be changed at any time.
 */
class LayerFilter : public LayerProperty {
 public:
  /**
   * Applies the filter to the scaled Image of the layer content and draws it on the canvas.
   * @param contentScale The scale factor of the source Image relative to its original size.
   * Some filters have size-related parameters that must be adjusted with this scale factor.
   * @return True if the filter was applied and drawn, false otherwise.
   */
  virtual bool applyFilter(Canvas* canvas, std::shared_ptr<Image> image, float contentScale) = 0;

  /**
   * Returns the bounds after applying the filter to the scaled layer bounds
   * @param contentScale The scale factor of the source bounds relative to its original size.
   * Some filters have size-related parameters that must be adjusted with this scale factor
   * @return The bounds of the filtered image.
   */
  virtual Rect filterBounds(const Rect& srcRect, float contentScale) = 0;
};
}  // namespace tgfx
