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
class ImageFilter {
 public:
  static std::shared_ptr<ImageFilter> Blur(float blurrinessX, float blurrinessY,
                                           TileMode tileMode = TileMode::Decal,
                                           const Rect* cropRect = nullptr);

  static std::shared_ptr<ImageFilter> DropShadow(float dx, float dy, float blurrinessX,
                                                 float blurrinessY, const Color& color,
                                                 const Rect* cropRect = nullptr);

  static std::shared_ptr<ImageFilter> DropShadowOnly(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color,
                                                     const Rect* cropRect = nullptr);

  virtual ~ImageFilter();

  /**
   * Returns the bounds of the filtered image by the given bounds of the source image.
   */
  Rect filterBounds(const Rect& rect) const;

 protected:
  explicit ImageFilter(const Rect* cropRect);

  bool applyCropRect(const Rect& srcRect, Rect* dstRect, const Rect* clipRect = nullptr) const;

  virtual Rect onFilterBounds(const Rect& srcRect) const;

  virtual std::unique_ptr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                                 const ImageFPArgs& args,
                                                                 const Matrix* localMatrix,
                                                                 const Rect* subset) const;

 private:
  Rect* cropRect = nullptr;

  friend class FilterImage;
};
}  // namespace tgfx
