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

#include "tgfx/core/Image.h"
#include "core/images/BufferImage.h"
#include "core/images/FilterImage.h"
#include "core/images/GeneratorImage.h"
#include "core/images/OrientImage.h"
#include "core/images/RGBAAAImage.h"
#include "core/images/RasterImage.h"
#include "core/images/ScaleImage.h"
#include "core/images/SubsetImage.h"
#include "core/images/TextureImage.h"
#include "gpu/OpContext.h"
#include "gpu/ProxyProvider.h"
#include "gpu/TPArgs.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {
class PixelDataConverter : public ImageGenerator {
 public:
  PixelDataConverter(const ImageInfo& info, std::shared_ptr<Data> pixels)
      : ImageGenerator(info.width(), info.height()), info(info), pixels(std::move(pixels)) {
  }

  bool isAlphaOnly() const override {
    return info.isAlphaOnly();
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override {
    Bitmap bitmap(width(), height(), isAlphaOnly(), tryHardware);
    if (bitmap.isEmpty()) {
      return nullptr;
    }
    auto success = bitmap.writePixels(info, pixels->data());
    if (!success) {
      return nullptr;
    }
    return bitmap.makeBuffer();
  }

 private:
  ImageInfo info = {};
  std::shared_ptr<Data> pixels = nullptr;
};

std::shared_ptr<Image> Image::MakeFromFile(const std::string& filePath) {
  auto codec = ImageCodec::MakeFrom(filePath);
  auto image = MakeFrom(codec);
  if (image == nullptr) {
    return nullptr;
  }
  return image->makeOriented(codec->orientation());
}

std::shared_ptr<Image> Image::MakeFromEncoded(std::shared_ptr<Data> encodedData) {
  auto codec = ImageCodec::MakeFrom(std::move(encodedData));
  auto image = MakeFrom(codec);
  if (image == nullptr) {
    return nullptr;
  }
  return image->makeOriented(codec->orientation());
}

std::shared_ptr<Image> Image::MakeFrom(NativeImageRef nativeImage) {
  auto codec = ImageCodec::MakeFrom(nativeImage);
  auto image = MakeFrom(codec);
  if (image == nullptr) {
    return nullptr;
  }
  return image->makeOriented(codec->orientation());
}

std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageGenerator> generator) {
  return GeneratorImage::MakeFrom(std::move(generator));
}

std::shared_ptr<Image> Image::MakeFrom(const ImageInfo& info, std::shared_ptr<Data> pixels) {
  if (info.isEmpty() || pixels == nullptr || info.byteSize() > pixels->size()) {
    return nullptr;
  }
  auto imageBuffer = ImageBuffer::MakeFrom(info, pixels);
  if (imageBuffer != nullptr) {
    return MakeFrom(std::move(imageBuffer));
  }
  auto converter = std::make_shared<PixelDataConverter>(info, std::move(pixels));
  return MakeFrom(std::move(converter));
}

std::shared_ptr<Image> Image::MakeFrom(const Bitmap& bitmap) {
  return MakeFrom(bitmap.makeBuffer());
}

std::shared_ptr<Image> Image::MakeFrom(HardwareBufferRef hardwareBuffer, YUVColorSpace colorSpace) {
  auto buffer = ImageBuffer::MakeFrom(hardwareBuffer, colorSpace);
  return MakeFrom(std::move(buffer));
}

std::shared_ptr<Image> Image::MakeI420(std::shared_ptr<YUVData> yuvData, YUVColorSpace colorSpace) {
  auto buffer = ImageBuffer::MakeI420(std::move(yuvData), colorSpace);
  return MakeFrom(std::move(buffer));
}

std::shared_ptr<Image> Image::MakeNV12(std::shared_ptr<YUVData> yuvData, YUVColorSpace colorSpace) {
  auto buffer = ImageBuffer::MakeNV12(std::move(yuvData), colorSpace);
  return MakeFrom(std::move(buffer));
}

std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageBuffer> imageBuffer) {
  return BufferImage::MakeFrom(std::move(imageBuffer));
}

std::shared_ptr<Image> Image::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                       ImageOrigin origin) {
  if (context == nullptr) {
    return nullptr;
  }
  auto textureProxy = context->proxyProvider()->wrapBackendTexture(backendTexture, origin, false);
  return TextureImage::Wrap(std::move(textureProxy));
}

std::shared_ptr<Image> Image::MakeAdopted(Context* context, const BackendTexture& backendTexture,
                                          ImageOrigin origin) {
  if (context == nullptr) {
    return nullptr;
  }
  auto textureProxy = context->proxyProvider()->wrapBackendTexture(backendTexture, origin, true);
  return TextureImage::Wrap(std::move(textureProxy));
}

BackendTexture Image::getBackendTexture(Context*, ImageOrigin*) const {
  return {};
}

std::shared_ptr<Image> Image::makeRasterized(bool mipmapped,
                                             const SamplingOptions& sampling) const {
  return RasterImage::MakeFrom(weakThis.lock(), mipmapped, sampling);
}

std::shared_ptr<Image> Image::makeTextureImage(Context* context,
                                               const SamplingOptions& sampling) const {
  TPArgs args(context, 0, hasMipmaps());
  return TextureImage::Wrap(lockTextureProxy(args, sampling));
}

std::shared_ptr<Image> Image::makeDecoded(Context* context) const {
  if (isFullyDecoded()) {
    return weakThis.lock();
  }
  auto decoded = onMakeDecoded(context);
  if (decoded == nullptr) {
    return weakThis.lock();
  }
  return decoded;
}

std::shared_ptr<Image> Image::onMakeDecoded(Context*, bool) const {
  return nullptr;
}

std::shared_ptr<Image> Image::makeMipmapped(bool enabled) const {
  if (hasMipmaps() == enabled) {
    return weakThis.lock();
  }
  return onMakeMipmapped(enabled);
}

std::shared_ptr<Image> Image::makeSubset(const Rect& subset) const {
  auto rect = subset;
  rect.round();
  auto bounds = Rect::MakeWH(width(), height());
  if (bounds == rect) {
    return weakThis.lock();
  }
  if (!bounds.contains(rect)) {
    return nullptr;
  }
  return onMakeSubset(rect);
}

std::shared_ptr<Image> Image::makeScaled(float scaleX, float scaleY) const {
  auto w = width();
  auto h = height();
  auto scaledWidth = ScaleImage::GetSize(w, scaleX);
  auto scaledHeight = ScaleImage::GetSize(h, scaleY);
  if (scaledWidth == w && scaledHeight == h) {
    return weakThis.lock();
  }
  return onMakeScaled(scaleX, scaleY);
}

std::shared_ptr<Image> Image::onMakeSubset(const Rect& subset) const {
  return SubsetImage::MakeFrom(weakThis.lock(), subset);
}

std::shared_ptr<Image> Image::makeOriented(Orientation orientation) const {
  if (orientation == Orientation::TopLeft) {
    return weakThis.lock();
  }
  return onMakeOriented(orientation);
}

std::shared_ptr<Image> Image::onMakeOriented(Orientation orientation) const {
  return OrientImage::MakeFrom(weakThis.lock(), orientation);
}

std::shared_ptr<Image> Image::onMakeScaled(float scaleX, float scaleY) const {
  return ScaleImage::MakeFrom(weakThis.lock(), Point::Make(scaleX, scaleY));
}

std::shared_ptr<Image> Image::makeWithFilter(std::shared_ptr<ImageFilter> filter, Point* offset,
                                             const Rect* clipRect) const {
  return onMakeWithFilter(std::move(filter), offset, clipRect);
}

std::shared_ptr<Image> Image::onMakeWithFilter(std::shared_ptr<ImageFilter> filter, Point* offset,
                                               const Rect* clipRect) const {
  return FilterImage::MakeFrom(weakThis.lock(), std::move(filter), offset, clipRect);
}

std::shared_ptr<Image> Image::makeRGBAAA(int displayWidth, int displayHeight, int alphaStartX,
                                         int alphaStartY) const {
  if (alphaStartX == 0 && alphaStartY == 0) {
    return makeSubset(Rect::MakeWH(displayWidth, displayHeight));
  }
  return RGBAAAImage::MakeFrom(weakThis.lock(), displayWidth, displayHeight, alphaStartX,
                               alphaStartY);
}

std::shared_ptr<TextureProxy> Image::lockTextureProxy(const TPArgs& args,
                                                      const SamplingOptions& sampling) const {
  auto context = args.context;
  auto alphaRenderable = context->caps()->isFormatRenderable(PixelFormat::ALPHA_8);
  auto format = isAlphaOnly() && alphaRenderable ? PixelFormat::ALPHA_8 : PixelFormat::RGBA_8888;
  auto proxyProvider = context->proxyProvider();
  // Don't pass args.renderFlags to this method. It's meant for creating the fragment processor.
  // The caller should decide whether to disable the cache by setting args.uniqueKey.
  auto textureProxy =
      proxyProvider->createTextureProxy(args.uniqueKey, width(), height(), format, args.mipmapped);
  auto renderTarget = proxyProvider->createRenderTargetProxy(textureProxy, format);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto drawRect = Rect::MakeWH(width(), height());
  FPArgs fpArgs(args.context, args.renderFlags, drawRect, Matrix::I());
  auto processor = FragmentProcessor::Make(weakThis.lock(), fpArgs, sampling);
  if (processor == nullptr) {
    return nullptr;
  }
  OpContext opContext(renderTarget, true);
  opContext.fillWithFP(std::move(processor), Matrix::I());
  return textureProxy;
}
}  // namespace tgfx
