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

#include "tgfx/core/Image.h"
#include <memory>
#include "core/images/CodecImage.h"
#include "core/images/FilterImage.h"
#include "core/images/OrientImage.h"
#include "core/images/RGBAAAImage.h"
#include "core/images/RasterizedImage.h"
#include "core/images/ScaledImage.h"
#include "core/images/SubsetImage.h"
#include "core/images/TextureImage.h"
#include "core/utils/WeakMap.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RenderContext.h"
#include "gpu/TPArgs.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Pixmap.h"

namespace tgfx {

static std::shared_ptr<ColorSpace> MakeColorSpaceFromYUVColorSpace(YUVColorSpace yuvColorSpace) {
  ColorMatrix33 matrix{};
  switch (yuvColorSpace) {
    case YUVColorSpace::BT601_FULL:
    case YUVColorSpace::BT601_LIMITED:
    case YUVColorSpace::JPEG_FULL:
      NamedPrimaries::Rec601.toXYZD50(&matrix);
      return ColorSpace::MakeRGB(NamedTransferFunction::Rec601, matrix);
    case YUVColorSpace::BT709_FULL:
    case YUVColorSpace::BT709_LIMITED:
      NamedPrimaries::Rec709.toXYZD50(&matrix);
      return ColorSpace::MakeRGB(NamedTransferFunction::Rec709, matrix);
    case YUVColorSpace::BT2020_FULL:
    case YUVColorSpace::BT2020_LIMITED:
      return ColorSpace::MakeRGB(NamedTransferFunction::Rec2020, NamedGamut::Rec2020);
  }
}

std::shared_ptr<Image> Image::MakeFromFile(const std::string& filePath) {
  static WeakMap<std::string, Image> imageMap = {};
  if (filePath.empty()) {
    return nullptr;
  }
  if (auto cached = imageMap.find(filePath)) {
    return cached;
  }
  auto codec = ImageCodec::MakeFrom(filePath);
  auto image = MakeFrom(codec);
  if (image != nullptr) {
    imageMap.insert(filePath, image);
  }
  return image;
}

std::shared_ptr<Image> Image::MakeFromEncoded(std::shared_ptr<Data> encodedData) {
  auto codec = ImageCodec::MakeFrom(std::move(encodedData));
  return MakeFrom(std::move(codec));
}

std::shared_ptr<Image> Image::MakeFrom(NativeImageRef nativeImage) {
  auto codec = ImageCodec::MakeFrom(nativeImage);
  return MakeFrom(std::move(codec));
}

std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageGenerator> generator) {
  if (generator == nullptr) {
    return nullptr;
  }
  std::shared_ptr<Image> image = nullptr;
  if (generator->isImageCodec()) {
    auto codec = std::static_pointer_cast<ImageCodec>(generator);
    auto orientation = codec->orientation();
    image = std::make_shared<CodecImage>(std::move(codec), codec->width(), codec->height(), false);
    image->weakThis = image;
    image = image->makeRasterized();
    image = image->makeOriented(orientation);
  } else {
    image = std::make_shared<GeneratorImage>(std::move(generator), false);
    image->weakThis = image;
    image = image->makeRasterized();
  }
  return image;
}

std::shared_ptr<Image> Image::MakeFrom(const ImageInfo& info, std::shared_ptr<Data> pixels,
                                       std::shared_ptr<ColorSpace> colorSpace) {
  auto codec = ImageCodec::MakeFrom(info, std::move(pixels), std::move(colorSpace));
  return MakeFrom(std::move(codec));
}

std::shared_ptr<Image> Image::MakeFrom(const Bitmap& bitmap,
                                       std::shared_ptr<ColorSpace> colorSpace) {
  return MakeFrom(bitmap.makeBuffer(), std::move(colorSpace));
}

std::shared_ptr<Image> Image::MakeFrom(HardwareBufferRef hardwareBuffer, YUVColorSpace colorSpace) {
  auto buffer = ImageBuffer::MakeFrom(hardwareBuffer, colorSpace);
  return MakeFrom(std::move(buffer), MakeColorSpaceFromYUVColorSpace(colorSpace));
}

std::shared_ptr<Image> Image::MakeI420(std::shared_ptr<YUVData> yuvData, YUVColorSpace colorSpace) {
  auto buffer = ImageBuffer::MakeI420(std::move(yuvData), colorSpace);
  return MakeFrom(std::move(buffer), MakeColorSpaceFromYUVColorSpace(colorSpace));
}

std::shared_ptr<Image> Image::MakeNV12(std::shared_ptr<YUVData> yuvData, YUVColorSpace colorSpace) {
  auto buffer = ImageBuffer::MakeNV12(std::move(yuvData), colorSpace);
  return MakeFrom(std::move(buffer), MakeColorSpaceFromYUVColorSpace(colorSpace));
}

std::shared_ptr<Image> Image::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                       ImageOrigin origin, std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto textureProxy = context->proxyProvider()->wrapExternalTexture(backendTexture, origin, false);
  return TextureImage::Wrap(std::move(textureProxy), std::move(colorSpace));
}

std::shared_ptr<Image> Image::MakeAdopted(Context* context, const BackendTexture& backendTexture,
                                          ImageOrigin origin,
                                          std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto textureProxy = context->proxyProvider()->wrapExternalTexture(backendTexture, origin, true);
  return TextureImage::Wrap(std::move(textureProxy), std::move(colorSpace));
}

std::shared_ptr<Image> Image::makeTextureImage(Context* context) const {
  if (context == nullptr) {
    return nullptr;
  }
  TPArgs args(context, 0, hasMipmaps(), 1.0f, BackingFit::Exact);
  auto textureProxy = lockTextureProxy(args);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  return TextureImage::Wrap(std::move(textureProxy), colorSpace());
}

BackendTexture Image::getBackendTexture(Context*, ImageOrigin*) const {
  return {};
}

float Image::getRasterizedScale(float) const {
  return 1.0f;
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
  if (rect.isEmpty()) {
    return nullptr;
  }
  auto bounds = Rect::MakeWH(width(), height());
  if (bounds == rect) {
    return weakThis.lock();
  }
  if (!bounds.contains(rect)) {
    return nullptr;
  }
  return onMakeSubset(rect);
}

std::shared_ptr<Image> Image::makeScaled(int newWidth, int newHeight,
                                         const SamplingOptions& sampling) const {
  if (newWidth <= 0 || newHeight <= 0) {
    return nullptr;
  }
  if (newWidth == width() && newHeight == height()) {
    return weakThis.lock();
  }
  return onMakeScaled(newWidth, newHeight, sampling);
}

std::shared_ptr<Image> Image::makeRasterized() const {
  auto result = std::make_shared<RasterizedImage>(UniqueKey::Make(), weakThis.lock());
  result->weakThis = result;
  return result;
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

std::shared_ptr<Image> Image::makeWithFilter(std::shared_ptr<ImageFilter> filter, Point* offset,
                                             const Rect* clipRect) const {
  return onMakeWithFilter(std::move(filter), offset, clipRect);
}

std::shared_ptr<Image> Image::onMakeWithFilter(std::shared_ptr<ImageFilter> filter, Point* offset,
                                               const Rect* clipRect) const {
  return FilterImage::MakeFrom(weakThis.lock(), std::move(filter), offset, clipRect);
}

std::shared_ptr<Image> Image::onMakeScaled(int newWidth, int newHeight,
                                           const SamplingOptions& sampling) const {
  auto scaledImage =
      std::make_shared<ScaledImage>(weakThis.lock(), newWidth, newHeight, sampling, hasMipmaps());
  scaledImage->weakThis = scaledImage;
  return scaledImage;
}

std::shared_ptr<Image> Image::makeRGBAAA(int displayWidth, int displayHeight, int alphaStartX,
                                         int alphaStartY) const {
  if (alphaStartX == 0 && alphaStartY == 0) {
    return makeSubset(Rect::MakeWH(displayWidth, displayHeight));
  }
  return RGBAAAImage::MakeFrom(weakThis.lock(), displayWidth, displayHeight, alphaStartX,
                               alphaStartY);
}
}  // namespace tgfx
