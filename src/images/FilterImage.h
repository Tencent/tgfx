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

#include "images/NestedImage.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {
/**
 * FilterImage wraps an existing image and applies an ImageFilter to it.
 */
class FilterImage : public NestedImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source,
                                         std::shared_ptr<ImageFilter> filter,
                                         Point* offset = nullptr);

  int width() const override {
    return static_cast<int>(bounds.width());
  }

  int height() const override {
    return static_cast<int>(bounds.height());
  }

 protected:
  FilterImage(std::shared_ptr<Image> source, std::shared_ptr<ImageFilter> filter,
              const Rect& bounds);

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const ImageFPArgs& args,
                                                         const Matrix* localMatrix,
                                                         const Rect* clipBounds) const override;

 private:
  std::shared_ptr<ImageFilter> filter = nullptr;
  Rect bounds = Rect::MakeEmpty();

  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source,
                                         std::shared_ptr<ImageFilter> filter, const Rect& bounds);
};
}  // namespace tgfx
