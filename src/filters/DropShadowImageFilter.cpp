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

#include "DropShadowImageFilter.h"
#include "gpu/Texture.h"
#include "gpu/processors/FragmentProcessor.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::DropShadow(float dx, float dy, float blurrinessX,
                                                     float blurrinessY, const Color& color,
                                                     const Rect* cropRect) {
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, false,
                                                 cropRect);
}

std::shared_ptr<ImageFilter> ImageFilter::DropShadowOnly(float dx, float dy, float blurrinessX,
                                                         float blurrinessY, const Color& color,
                                                         const Rect* cropRect) {
  return std::make_shared<DropShadowImageFilter>(dx, dy, blurrinessX, blurrinessY, color, true,
                                                 cropRect);
}

DropShadowImageFilter::DropShadowImageFilter(float dx, float dy, float blurrinessX,
                                             float blurrinessY, const Color& color, bool shadowOnly,
                                             const Rect* cropRect)
    : ImageFilter(cropRect), dx(dx), dy(dy), blurrinessX(blurrinessX), blurrinessY(blurrinessY),
      color(color), shadowOnly(shadowOnly) {
}

Rect DropShadowImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto bounds = srcRect;
  auto shadowBounds = bounds;
  shadowBounds.offset(dx, dy);
  if (auto filter = ImageFilter::Blur(blurrinessX, blurrinessY)) {
    shadowBounds = filter->filterBounds(shadowBounds);
  }
  if (!shadowOnly) {
    bounds.join(shadowBounds);
  } else {
    bounds = shadowBounds;
  }
  return bounds;
}

std::unique_ptr<FragmentProcessor> DropShadowImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const ImageFPArgs& args, const Matrix* localMatrix,
    const Rect* subset) const {
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  Rect dstBounds = Rect::MakeEmpty();
  if (!applyCropRect(inputBounds, &dstBounds, subset)) {
    return nullptr;
  }
  auto surface = Surface::Make(args.context, static_cast<int>(dstBounds.width()),
                               static_cast<int>(dstBounds.height()));
  if (surface == nullptr) {
    return nullptr;
  }
  auto offsetMatrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  auto canvas = surface->getCanvas();
  Paint paint;
  paint.setImageFilter(ImageFilter::Blur(blurrinessX, blurrinessY));
  paint.setColorFilter(ColorFilter::Blend(color, BlendMode::SrcIn));
  canvas->concat(offsetMatrix);
  canvas->save();
  canvas->concat(Matrix::MakeTrans(dx, dy));
  canvas->drawImage(source, &paint);
  canvas->restore();
  if (!shadowOnly) {
    canvas->drawImage(source);
  }
  auto image = surface->makeImageSnapshot();
  if (localMatrix != nullptr) {
    offsetMatrix.preConcat(*localMatrix);
  }
  return FragmentProcessor::MakeFromImage(image, args, &offsetMatrix);
}

}  // namespace tgfx
