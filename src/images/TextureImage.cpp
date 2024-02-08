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

#include "TextureImage.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "images/MipmapImage.h"
#include "images/RGBAAAImage.h"

namespace tgfx {
/**
 * TextureImage wraps an existing texture proxy.
 */
class TextureProxyImage : public TextureImage {
 public:
  explicit TextureProxyImage(std::shared_ptr<TextureProxy> textureProxy)
      : TextureImage(textureProxy->getResourceKey()), textureProxy(std::move(textureProxy)) {
  }

  int width() const override {
    return textureProxy->width();
  }

  int height() const override {
    return textureProxy->height();
  }

  bool isAlphaOnly() const override {
    return textureProxy->isAlphaOnly();
  }

  bool hasMipmaps() const override {
    return textureProxy->hasMipmaps();
  }

  bool isTextureBacked() const override {
    return true;
  }

  BackendTexture getBackendTexture(Context* context, ImageOrigin* origin = nullptr) const override {
    if (context == nullptr) {
      return {};
    }
    context->flush();
    auto texture = textureProxy->getTexture();
    if (texture == nullptr) {
      return {};
    }
    if (origin != nullptr) {
      *origin = textureProxy->origin();
    }
    return texture->getBackendTexture();
  }

  std::shared_ptr<Image> makeTextureImage(Context* context) const override {
    if (textureProxy->getContext() == context) {
      return std::static_pointer_cast<Image>(weakThis.lock());
    }
    return nullptr;
  }

 protected:
  std::shared_ptr<Image> onMakeMipmapped(bool) const override {
    return nullptr;
  }

  std::shared_ptr<TextureProxy> onLockTextureProxy(Context* context, const ResourceKey&, bool,
                                                   uint32_t) const override {
    if (textureProxy->getContext() != context) {
      return nullptr;
    }
    return textureProxy;
  }

 private:
  std::shared_ptr<TextureProxy> textureProxy = nullptr;
};

std::shared_ptr<Image> TextureImage::Wrap(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto textureImage = std::make_shared<TextureProxyImage>(std::move(textureProxy));
  textureImage->weakThis = textureImage;
  return textureImage;
}

TextureImage::TextureImage(ResourceKey resourceKey) : resourceKey(std::move(resourceKey)) {
}

std::shared_ptr<Image> TextureImage::makeRasterized(float rasterizationScale,
                                                    SamplingOptions sampling) const {
  if (rasterizationScale == 1.0f) {
    return weakThis.lock();
  }
  return Image::makeRasterized(rasterizationScale, sampling);
}

std::shared_ptr<Image> TextureImage::makeTextureImage(Context* context) const {
  return TextureImage::Wrap(lockTextureProxy(context));
}

std::shared_ptr<TextureProxy> TextureImage::lockTextureProxy(tgfx::Context* context,
                                                             uint32_t renderFlags) const {
  if (context == nullptr) {
    return nullptr;
  }
  return onLockTextureProxy(context, resourceKey, false, renderFlags);
}

std::shared_ptr<Image> TextureImage::onMakeMipmapped(bool enabled) const {
  auto source = std::static_pointer_cast<TextureImage>(weakThis.lock());
  return enabled ? MipmapImage::MakeFrom(std::move(source)) : source;
}

std::shared_ptr<Image> TextureImage::onMakeRGBAAA(int displayWidth, int displayHeight,
                                                  int alphaStartX, int alphaStartY) const {
  if (isAlphaOnly()) {
    return nullptr;
  }
  auto textureImage = std::static_pointer_cast<TextureImage>(weakThis.lock());
  return RGBAAAImage::MakeFrom(std::move(textureImage), displayWidth, displayHeight, alphaStartX,
                               alphaStartY);
}

std::unique_ptr<FragmentProcessor> TextureImage::asFragmentProcessor(const DrawArgs& args,
                                                                     const Matrix* localMatrix,
                                                                     TileMode tileModeX,
                                                                     TileMode tileModeY) const {
  auto proxy = lockTextureProxy(args.context, args.renderFlags);
  if (proxy == nullptr) {
    return nullptr;
  }
  auto processor =
      TiledTextureEffect::Make(proxy, tileModeX, tileModeY, args.sampling, localMatrix);
  if (isAlphaOnly() && !proxy->isAlphaOnly()) {
    return FragmentProcessor::MulInputByChildAlpha(std::move(processor));
  }
  return processor;
}
}  // namespace tgfx
