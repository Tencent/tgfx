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
#include "core/RasterBuffer.h"
#include "core/RasterGenerator.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/FillRectOp.h"
#include "images/BufferImage.h"
#include "images/FilterImage.h"
#include "images/GeneratorImage.h"
#include "images/RasterImage.h"
#include "images/SubsetImage.h"
#include "images/TextureImage.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
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
  auto imageBuffer = RasterBuffer::MakeFrom(info, pixels);
  if (imageBuffer != nullptr) {
    return MakeFrom(std::move(imageBuffer));
  }
  auto imageGenerator = RasterGenerator::MakeFrom(info, std::move(pixels));
  return MakeFrom(std::move(imageGenerator));
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

std::shared_ptr<Image> Image::makeRasterized(float rasterizationScale,
                                             SamplingOptions sampling) const {
  return RasterImage::MakeFrom(weakThis.lock(), rasterizationScale, sampling);
}

std::shared_ptr<Image> Image::makeTextureImage(Context* context) const {
  auto surface = Surface::Make(context, width(), height(), isAlphaOnly(), 1, hasMipmaps());
  if (surface == nullptr) {
    return nullptr;
  }
  auto canvas = surface->getCanvas();
  canvas->drawImage(weakThis.lock(), 0, 0);
  return surface->makeImageSnapshot();
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

std::shared_ptr<Image> Image::onMakeSubset(const Rect& subset) const {
  return SubsetImage::MakeFrom(weakThis.lock(), Orientation::TopLeft, subset);
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

std::shared_ptr<Image> Image::makeWithFilter(std::shared_ptr<Filter> filter, Point* offset,
                                             const Rect* clipRect) const {
  return FilterImage::MakeFrom(weakThis.lock(), std::move(filter), offset, clipRect);
}

std::shared_ptr<Image> Image::makeRGBAAA(int displayWidth, int displayHeight, int alphaStartX,
                                         int alphaStartY) const {
  if (alphaStartX == 0 && alphaStartY == 0) {
    return makeSubset(Rect::MakeWH(displayWidth, displayHeight));
  }
  return onMakeRGBAAA(displayWidth, displayHeight, alphaStartX, alphaStartY);
}

std::shared_ptr<Image> Image::onMakeRGBAAA(int, int, int, int) const {
  return nullptr;
}
}  // namespace tgfx
