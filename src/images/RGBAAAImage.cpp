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

#include "RGBAAAImage.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
std::shared_ptr<Image> RGBAAAImage::MakeFrom(std::shared_ptr<ResourceImage> source,
                                             int displayWidth, int displayHeight, int alphaStartX,
                                             int alphaStartY) {
  if (source == nullptr || alphaStartX + displayWidth > source->width() ||
      alphaStartY + displayHeight > source->height()) {
    return nullptr;
  }
  auto bounds = Rect::MakeWH(displayWidth, displayHeight);
  auto alphaStart = Point::Make(alphaStartX, alphaStartY);
  auto image = std::shared_ptr<RGBAAAImage>(
      new RGBAAAImage(std::move(source), Orientation::TopLeft, bounds, alphaStart));
  image->weakThis = image;
  return image;
}

RGBAAAImage::RGBAAAImage(std::shared_ptr<Image> source, Orientation orientation, const Rect& bounds,
                         const Point& alphaStart)
    : SubsetImage(std::move(source), orientation, Point::Make(1.0f, 1.0f), bounds),
      alphaStart(alphaStart) {
}

std::shared_ptr<Image> RGBAAAImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  auto image = std::shared_ptr<RGBAAAImage>(
      new RGBAAAImage(std::move(newSource), orientation, bounds, alphaStart));
  image->weakThis = image;
  return image;
}

std::unique_ptr<FragmentProcessor> RGBAAAImage::asFragmentProcessor(const FPArgs& args, TileMode,
                                                                    TileMode,
                                                                    const SamplingOptions& sampling,
                                                                    const Matrix* uvMatrix) const {
  auto proxy = source->lockTextureProxy(args.context, args.renderFlags);
  auto matrix = concatUVMatrix(uvMatrix);
  return TextureEffect::MakeRGBAAA(std::move(proxy), alphaStart, sampling, AddressOf(matrix));
}
}  // namespace tgfx
