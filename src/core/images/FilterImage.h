/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "core/images/SubsetImage.h"
#include "gpu/TPArgs.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {
/**
 * FilterImage wraps an existing image and applies an ImageFilter to it.
 */
class FilterImage : public SubsetImage {
 public:
  /**
   * Creates a new FilterImage from the given source image, filter, and clipRect.
   */
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source,
                                         std::shared_ptr<ImageFilter> filter,
                                         Point* offset = nullptr, const Rect* clipRect = nullptr);

  int width() const override {
    return static_cast<int>(bounds.width());
  }

  int height() const override {
    return static_cast<int>(bounds.height());
  }

  std::shared_ptr<ImageFilter> filter = nullptr;

  std::shared_ptr<ColorSpace> colorSpace() const override {
    return source->colorSpace();
  }

 protected:
  Type type() const override {
    return Type::Filter;
  }

  FilterImage(std::shared_ptr<Image> source, const Rect& bounds,
              std::shared_ptr<ImageFilter> filter);

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

  std::shared_ptr<Image> onMakeWithFilter(std::shared_ptr<ImageFilter> filter, Point* offset,
                                          const Rect* clipRect) const override;

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

 private:
  static std::shared_ptr<Image> Wrap(std::shared_ptr<Image> source, const Rect& bounds,
                                     std::shared_ptr<ImageFilter> filter);
};
}  // namespace tgfx
