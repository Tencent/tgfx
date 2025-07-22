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

#include "RGBAAAImage.h"
#include "core/utils/AddressOf.h"
#include "core/utils/Log.h"
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

PlacementPtr<FragmentProcessor> RGBAAAImage::asFragmentProcessor(const FPArgs& args,
                                                                 const SamplingArgs& samplingArgs,
                                                                 const Matrix* uvMatrix) const {
  DEBUG_ASSERT(!source->isAlphaOnly());
  auto matrix = concatUVMatrix(uvMatrix);
  auto drawBounds = args.drawRect;
  if (matrix) {
    matrix->mapRect(&drawBounds);
  }
  auto newSamplingArgs = samplingArgs;
  auto mipmapped = source->hasMipmaps() && samplingArgs.sampling.mipmapMode != MipmapMode::None;
  if (bounds.contains(drawBounds)) {
    if (samplingArgs.constraint != SrcRectConstraint::Strict) {
      newSamplingArgs.sampleArea = getSubset(drawBounds);
    }
    TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
    auto proxy = source->lockTextureProxy(tpArgs);
    return TextureEffect::MakeRGBAAA(std::move(proxy), newSamplingArgs, alphaStart,
                                     AddressOf(matrix));
  }
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
  auto textureProxy = lockTextureProxy(tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  newSamplingArgs.sampleArea = std::nullopt;
  return TiledTextureEffect::Make(textureProxy, newSamplingArgs, uvMatrix);
}

std::shared_ptr<Image> RGBAAAImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset.makeOffset(bounds.x(), bounds.y());
  auto image = std::shared_ptr<Image>(new RGBAAAImage(source, newBounds, alphaStart));
  image->weakThis = image;
  return image;
}

}  // namespace tgfx
