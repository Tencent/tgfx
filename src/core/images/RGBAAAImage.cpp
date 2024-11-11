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
#include "core/utils/Log.h"
#include "core/utils/NeedMipmaps.h"
#include "gpu/TPArgs.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
std::shared_ptr<Image> RGBAAAImage::MakeFrom(std::shared_ptr<Image> source, int displayWidth,
                                             int displayHeight, int alphaStartX, int alphaStartY) {
  if (source == nullptr || source->isAlphaOnly() || alphaStartX + displayWidth > source->width() ||
      alphaStartY + displayHeight > source->height()) {
    return nullptr;
  }
  auto bounds = Rect::MakeWH(displayWidth, displayHeight);
  auto alphaStart = Point::Make(alphaStartX, alphaStartY);
  auto image = std::shared_ptr<RGBAAAImage>(new RGBAAAImage(std::move(source), bounds, alphaStart));
  image->weakThis = image;
  return image;
}

RGBAAAImage::RGBAAAImage(std::shared_ptr<Image> source, const Rect& bounds, const Point& alphaStart)
    : SubsetImage(std::move(source), bounds), alphaStart(alphaStart) {
}

std::shared_ptr<Image> RGBAAAImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  auto image =
      std::shared_ptr<RGBAAAImage>(new RGBAAAImage(std::move(newSource), bounds, alphaStart));
  image->weakThis = image;
  return image;
}

std::unique_ptr<FragmentProcessor> RGBAAAImage::asFragmentProcessor(const FPArgs& args,
                                                                    TileMode tileModeX,
                                                                    TileMode tileModeY,
                                                                    const SamplingOptions& sampling,
                                                                    const Matrix* uvMatrix) const {
  DEBUG_ASSERT(!source->isAlphaOnly());
  auto matrix = concatUVMatrix(uvMatrix);
  auto drawBounds = args.drawRect;
  if (matrix) {
    matrix->mapRect(&drawBounds);
  }
  auto mipmapped = source->hasMipmaps() && NeedMipmaps(sampling, args.viewMatrix, uvMatrix);
  if (bounds.contains(drawBounds)) {
    TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
    auto proxy = source->lockTextureProxy(tpArgs, sampling);
    return TextureEffect::MakeRGBAAA(std::move(proxy), alphaStart, sampling, AddressOf(matrix));
  }
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
  auto textureProxy = lockTextureProxy(tpArgs, sampling);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return TiledTextureEffect::Make(textureProxy, tileModeX, tileModeY, sampling, uvMatrix);
}

std::shared_ptr<Image> RGBAAAImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset.makeOffset(bounds.x(), bounds.y());
  auto image = std::shared_ptr<Image>(new RGBAAAImage(source, newBounds, alphaStart));
  image->weakThis = image;
  return image;
}

}  // namespace tgfx
