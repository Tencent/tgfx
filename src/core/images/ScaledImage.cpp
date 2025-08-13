/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ScaledImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

ScaledImage::ScaledImage(std::shared_ptr<Image> image, int width, int height,
                         const SamplingOptions& sampling, bool mipmapped)
    : TransformImage(std::move(image)), _width(width), _height(height), sampling(sampling),
      mipmapped(mipmapped) {
}

PlacementPtr<FragmentProcessor> ScaledImage::asFragmentProcessor(const FPArgs& args,
                                                                 const SamplingArgs& samplingArgs,
                                                                 const Matrix* uvMatrix) const {
  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  auto drawRect = Rect::MakeWH(_width, _height);
  if (!drawRect.intersect(drawBounds)) {
    return nullptr;
  }
  drawRect.roundOut();
  auto mipmapped = hasMipmaps() && samplingArgs.sampling.mipmapMode != MipmapMode::None;
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped, args.drawScale);
  auto textureProxy = lockTextureProxySubset(tpArgs, drawRect, sampling);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto extraMatrix = Matrix::MakeTrans(-drawRect.left, -drawRect.top);
  extraMatrix.preScale(drawRect.width() / static_cast<float>(textureProxy->width()),
                       drawRect.height() / static_cast<float>(textureProxy->height()));

  auto fpMatrix = extraMatrix;
  if (uvMatrix) {
    fpMatrix.preConcat(*uvMatrix);
  }
  auto newSamplingArgs = samplingArgs;
  newSamplingArgs.sampleArea = std::nullopt;
  return TiledTextureEffect::Make(textureProxy, newSamplingArgs, &fpMatrix, isAlphaOnly());
}

std::shared_ptr<TextureProxy> ScaledImage::lockTextureProxy(const TPArgs& args) const {
  return lockTextureProxySubset(args, Rect::MakeWH(width(), height()), sampling);
}

std::shared_ptr<Image> ScaledImage::onMakeScaled(int newWidth, int newHeight,
                                                 const SamplingOptions& sampling) const {
  if (newWidth == source->width() && newHeight == source->height()) {
    return source;
  }
  auto result = source->makeScaled(newWidth, newHeight, sampling);
  return result->makeMipmapped(hasMipmaps());
}

std::shared_ptr<Image> ScaledImage::onMakeMipmapped(bool enabled) const {
  auto scaledImage = std::make_shared<ScaledImage>(source, _width, _height, sampling, enabled);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

std::shared_ptr<Image> ScaledImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  auto scaledImage = std::make_shared<ScaledImage>(newSource, _width, _height, sampling, mipmapped);
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

std::optional<Matrix> ScaledImage::concatUVMatrix(const Matrix* uvMatrix) const {
  Matrix result =
      Matrix::MakeScale(static_cast<float>(source->width()) / static_cast<float>(_width),
                        static_cast<float>(source->height()) / static_cast<float>(_height));
  if (uvMatrix) {
    result.preConcat(*uvMatrix);
  }
  return result;
}

}  // namespace tgfx
