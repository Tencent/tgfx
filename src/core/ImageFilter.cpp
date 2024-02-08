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
#include "gpu/DrawingManager.h"
#include "gpu/ops/FillRectOp.h"
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

Rect ImageFilter::filterBounds(const Rect& rect) const {
  auto dstBounds = Rect::MakeEmpty();
  applyCropRect(rect, &dstBounds);
  return dstBounds;
}

bool ImageFilter::applyCropRect(const Rect& srcRect, Rect* dstRect, const Rect* clipBounds) const {
  *dstRect = onFilterBounds(srcRect);
  if (cropRect) {
    if (!dstRect->intersect(*cropRect)) {
      return false;
    }
  }
  if (clipBounds) {
    if (!dstRect->intersect(*clipBounds)) {
      return false;
    }
  }
  dstRect->roundOut();
  return true;
}

Rect ImageFilter::onFilterBounds(const Rect& srcRect) const {
  return srcRect;
}

std::unique_ptr<DrawOp> ImageFilter::makeDrawOp(std::shared_ptr<Image> source, const DrawArgs& args,
                                                const Matrix* localMatrix, TileMode tileModeX,
                                                TileMode tileModeY) const {
  auto processor = asFragmentProcessor(std::move(source), args, localMatrix, tileModeX, tileModeY);
  if (processor == nullptr) {
    return nullptr;
  }
  auto drawOp = FillRectOp::Make(args.color, args.drawRect, args.viewMatrix);
  drawOp->addColorFP(std::move(processor));
  return drawOp;
}

std::unique_ptr<FragmentProcessor> ImageFilter::asFragmentProcessor(std::shared_ptr<Image> source,
                                                                    const DrawArgs& args,
                                                                    const Matrix* localMatrix,
                                                                    TileMode tileModeX,
                                                                    TileMode tileModeY) const {
  if (source == nullptr) {
    return nullptr;
  }
  return source->asFragmentProcessor(args, localMatrix, tileModeX, tileModeY);
}
}  // namespace tgfx
