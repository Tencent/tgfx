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

#include "RasterizedImage.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {
RasterizedImage::RasterizedImage(UniqueKey uniqueKey, std::shared_ptr<Image> source)
    : uniqueKey(std::move(uniqueKey)), source(std::move(source)) {
}

std::shared_ptr<Image> RasterizedImage::makeRasterized() const {
  return weakThis.lock();
}

std::shared_ptr<TextureProxy> RasterizedImage::lockTextureProxy(const TPArgs& args) const {
  auto proxyProvider = args.context->proxyProvider();
  // If the image is mipmapped, we always cache the texture at the original scale.
  auto newScale = args.mipmapped ? 1.0f : source->getRasterizedScale(args.drawScale);
  auto textureKey = getTextureKey(newScale);
  auto textureProxy = proxyProvider->findOrWrapTextureProxy(textureKey);
  if (textureProxy != nullptr) {
    return textureProxy;
  }
  auto newArgs = args;
  newArgs.backingFit = BackingFit::Exact;
  newArgs.drawScale = newScale;
  textureProxy = source->lockTextureProxy(newArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  proxyProvider->assignProxyUniqueKey(textureProxy, textureKey);
  if (!(args.renderFlags & RenderFlags::DisableCache)) {
    textureProxy->assignUniqueKey(textureKey);
  }
  return textureProxy;
}

PlacementPtr<FragmentProcessor> RasterizedImage::asFragmentProcessor(
    const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix) const {
  auto textureProxy = lockTextureProxy(
      TPArgs(args.context, args.renderFlags, hasMipmaps(), args.drawScale, BackingFit::Exact));
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto fpMatrix =
      Matrix::MakeScale(static_cast<float>(textureProxy->width()) / static_cast<float>(width()),
                        static_cast<float>(textureProxy->height()) / static_cast<float>(height()));
  SamplingArgs newSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea) {
    Rect subset = *samplingArgs.sampleArea;
    fpMatrix.mapRect(&subset);
    newSamplingArgs.sampleArea = subset;
  }
  if (uvMatrix) {
    fpMatrix.preConcat(*uvMatrix);
  }
  return TiledTextureEffect::Make(std::move(textureProxy), newSamplingArgs, &fpMatrix,
                                  isAlphaOnly());
}

std::shared_ptr<Image> RasterizedImage::onMakeScaled(int newWidth, int newHeight,
                                                     const SamplingOptions& sampling) const {
  auto newSource = source->makeScaled(newWidth, newHeight, sampling);
  return newSource->makeRasterized();
}

std::shared_ptr<Image> RasterizedImage::onMakeDecoded(Context* context, bool tryHardware) const {
  auto key = getTextureKey();
  if (context != nullptr) {
    auto proxy = context->proxyProvider()->findProxy(key);
    if (proxy != nullptr) {
      return nullptr;
    }
    if (context->resourceCache()->hasUniqueResource(key)) {
      return nullptr;
    }
  }
  auto newSource = source->onMakeDecoded(context, tryHardware);
  if (newSource == nullptr) {
    return nullptr;
  }
  return newSource->makeRasterized();
}

std::shared_ptr<Image> RasterizedImage::onMakeMipmapped(bool enabled) const {
  auto newSource = source->makeMipmapped(enabled);
  auto image = std::make_shared<RasterizedImage>(uniqueKey, std::move(newSource));
  image->weakThis = image;
  return image;
}

UniqueKey RasterizedImage::getTextureKey(float cacheScale) const {
  BytesKey byteKey = {};
  if (hasMipmaps()) {
    static const auto MipmapFlag = UniqueID::Next();
    byteKey.write(MipmapFlag);
  }
  if (cacheScale < 1.f) {
    byteKey.write(cacheScale);
  }
  return UniqueKey::Append(uniqueKey, byteKey.data(), byteKey.size());
}

}  // namespace tgfx
