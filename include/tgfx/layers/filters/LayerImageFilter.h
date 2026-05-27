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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * LayerImageFilter is the base class for layer filters whose effect is expressed as an
 * ImageFilter. Subclasses override onCreateImageFilter() to return the ImageFilter for a given
 * content scale.
 */
class LayerImageFilter : public LayerFilter {
 public:
  Rect filterBounds(const Rect& srcRect, float contentScale,
                    MapDirection direction = MapDirection::Forward) override;

 protected:
  /**
   * Subclasses must override this method to create the ImageFilter for the given content scale.
   * The returned filter is cached until invalidateFilter() is called or the scale changes.
   */
  virtual std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) = 0;

  std::shared_ptr<Image> onFilterImage(std::shared_ptr<Image> input, float scale,
                                       const Rect& contentBounds, const Rect* clipBounds,
                                       Point* offset) override;

  void onInvalidateFilter() override;

 private:
  std::shared_ptr<ImageFilter> getImageFilter(float scale);

  bool dirty = true;
  float lastScale = 1.0f;
  std::shared_ptr<ImageFilter> lastFilter;
};

}  // namespace tgfx
