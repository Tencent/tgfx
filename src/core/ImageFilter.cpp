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

#include "tgfx/core/ImageFilter.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
ImageFilter::ImageFilter(const Rect* rect) {
  if (rect != nullptr) {
    cropRect = new Rect(*rect);
    cropRect->roundOut();
  }
}

ImageFilter::~ImageFilter() {
  delete cropRect;
}

bool ImageFilter::applyCropRect(const Rect& srcRect, Rect* dstRect, const Rect* clipRect) const {
  *dstRect = onFilterBounds(srcRect);
  dstRect->roundOut();
  if (cropRect) {
    if (!dstRect->intersect(*cropRect)) {
      return false;
    }
  }
  if (clipRect) {
    return dstRect->intersect(*clipRect);
  }
  return true;
}

Rect ImageFilter::filterBounds(const Rect& rect) const {
  auto dstBounds = Rect::MakeEmpty();
  applyCropRect(rect, &dstBounds);
  return dstBounds;
}

Rect ImageFilter::onFilterBounds(const Rect& srcRect) const {
  return srcRect;
}

std::unique_ptr<FragmentProcessor> ImageFilter::asFragmentProcessor(std::shared_ptr<Image> source,
                                                                    const ImageFPArgs& args,
                                                                    const Matrix* localMatrix,
                                                                    const Rect* subset) const {
  return FragmentProcessor::MakeFromImage(std::move(source), args, localMatrix, subset);
}
}  // namespace tgfx
