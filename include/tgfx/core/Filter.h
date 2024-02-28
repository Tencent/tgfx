/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
/**
 * Filter is the base class for all filters, such as ImageFilter, ColorFilter, and MaskFilter.
 */
class Filter {
 public:
  virtual ~Filter() = default;

  /**
   * Returns the bounds of the filtered image by the given bounds of the source image.
   */
  virtual Rect filterBounds(const Rect& rect) const {
    return rect;
  }

 protected:
  /**
   * The returned processor is in the coordinate space of the source image.
   */
  virtual std::unique_ptr<FragmentProcessor> onFilterImage(std::shared_ptr<Image> source,
                                                           const DrawArgs& args, TileMode tileModeX,
                                                           TileMode tileModeY,
                                                           const SamplingOptions& sampling,
                                                           const Matrix* localMatrix) const = 0;

  friend class FilterImage;
};
}  // namespace tgfx
