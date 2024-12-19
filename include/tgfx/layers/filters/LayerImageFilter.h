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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

class LayerImageFilter : public LayerFilter {
 public:
  /**
    * Applies the filter to the given picture and draws it to the canvas.
    * @param scale The scale factor to apply to the filter.
    * @return True if the filter was applied and drawn, false otherwise.
    */
  bool applyFilter(Canvas* canvas, std::shared_ptr<Picture> picture, float scale) override;

  /**
    * Return the bounds of after applying the filter to the given bounds.
    * @param scale The scale factor to apply to the filter.
    * @return The bounds of the filtered image.
    */
  Rect filterBounds(const Rect& srcRect, float scale) override;

 protected:
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
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

  bool dirty = true;

  float lastScale = 1.0f;

  std::shared_ptr<ImageFilter> lastFilter;
};
}  // namespace tgfx
