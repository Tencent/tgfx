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

#include "tgfx/core/ImageCodec.h"
#include "BoxFilterDownsample.h"
#include "core/PixelBuffer.h"
#include "core/utils/USE.h"
#include "core/utils/WeakMap.h"
#include "tgfx/core/Buffer.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/Stream.h"

#if defined(TGFX_USE_WEBP_DECODE) || defined(TGFX_USE_WEBP_ENCODE)
#include "core/codecs/webp/WebpCodec.h"
#endif

#if defined(TGFX_USE_PNG_DECODE) || defined(TGFX_USE_PNG_ENCODE)

#include "core/codecs/png/PngCodec.h"

#endif

#if defined(TGFX_USE_JPEG_DECODE) || defined(TGFX_USE_JPEG_ENCODE)
#include "core/codecs/jpeg/JpegCodec.h"
#endif

namespace tgfx {

static size_t GetPaddingAlignment16(size_t size) {
  auto remainder = size & 0xF;
  return (16 - remainder) & 0xF;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(const std::string& filePath) {
  static WeakMap<std::string, ImageCodec> imageCodecMap = {};
  if (filePath.empty()) {
    return nullptr;
  }
  if (auto cached = imageCodecMap.find(filePath)) {
    return cached;
  }
  std::shared_ptr<ImageCodec> codec = nullptr;
  auto stream = Stream::MakeFromFile(filePath);
  if (stream && stream->size() > 14) {
    Buffer buffer(14);
    if (stream->read(buffer.data(), 14) == 14) {
      auto data = buffer.release();
#ifdef TGFX_USE_WEBP_DECODE
      if (WebpCodec::IsWebp(data)) {
        codec = WebpCodec::MakeFrom(filePath);
      }
#endif

#ifdef TGFX_USE_PNG_DECODE
      if (codec == nullptr && PngCodec::IsPng(data)) {
        codec = PngCodec::MakeFrom(filePath);
      }
#endif

#ifdef TGFX_USE_JPEG_DECODE
      if (codec == nullptr && JpegCodec::IsJpeg(data)) {
        codec = JpegCodec::MakeFrom(filePath);
      }
#endif
    }
  }
  if (codec == nullptr) {
    codec = MakeNativeCodec(filePath);
  }
  if (codec && !ImageInfo::IsValidSize(codec->width(), codec->height())) {
    codec = nullptr;
  }
  if (codec) {
    imageCodecMap.insert(filePath, codec);
  }
  return codec;
}

std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(std::shared_ptr<Data> imageBytes) {
  if (imageBytes == nullptr || imageBytes->size() == 0) {
    return nullptr;
  }
  std::shared_ptr<ImageCodec> codec = nullptr;
#ifdef TGFX_USE_WEBP_DECODE
  if (WebpCodec::IsWebp(imageBytes)) {
    codec = WebpCodec::MakeFrom(imageBytes);
  }
#endif
#ifdef TGFX_USE_PNG_DECODE
  if (PngCodec::IsPng(imageBytes)) {
    codec = PngCodec::MakeFrom(imageBytes);
  }
#endif
#ifdef TGFX_USE_JPEG_DECODE
  if (JpegCodec::IsJpeg(imageBytes)) {
    codec = JpegCodec::MakeFrom(imageBytes);
  }
#endif
  if (codec == nullptr) {
    codec = MakeNativeCodec(imageBytes);
  }
  if (codec && !ImageInfo::IsValidSize(codec->width(), codec->height())) {
    codec = nullptr;
  }
  return codec;
}

std::shared_ptr<Data> ImageCodec::Encode(const Pixmap& pixmap, EncodedFormat format, int quality,
                                         std::shared_ptr<ColorSpace> colorSpace) {
  if (pixmap.isEmpty()) {
    return nullptr;
  }
  USE(format);
  if (quality > 100) {
    quality = 100;
  }
  if (quality < 0) {
    quality = 0;
  }
#ifdef TGFX_USE_JPEG_ENCODE
  if (format == EncodedFormat::JPEG) {
    return JpegCodec::Encode(pixmap, quality, std::move(colorSpace));
  }
#endif
#ifdef TGFX_USE_WEBP_ENCODE
  if (format == EncodedFormat::WEBP) {
    return WebpCodec::Encode(pixmap, quality, std::move(colorSpace));
  }
#endif
#ifdef TGFX_USE_PNG_ENCODE
  if (format == EncodedFormat::PNG) {
    return PngCodec::Encode(pixmap, quality, std::move(colorSpace));
  }
#endif
  (void)colorSpace;
  return nullptr;
}

bool ImageCodec::readPixels(const ImageInfo& dstInfo, void* dstPixels) const {
  if (dstInfo.width() > width() || dstInfo.height() > height()) {
    return false;
  }
  if (dstInfo.width() == width() && dstInfo.height() == height()) {
    return onReadPixels(dstInfo.colorType(), dstInfo.alphaType(), dstInfo.rowBytes(), dstPixels);
  }

  Buffer buffer = {};
  Buffer dstTempBuffer = {};
  auto dstData = dstPixels;
  auto dstImageInfo = dstInfo;
  auto colorType = dstInfo.colorType();
  auto srcRowBytes = dstInfo.bytesPerPixel() * static_cast<size_t>(width());
  if (dstInfo.colorType() != ColorType::RGBA_8888 && dstInfo.colorType() != ColorType::BGRA_8888 &&
      dstInfo.colorType() != ColorType::ALPHA_8 && dstInfo.colorType() != ColorType::Gray_8) {
    colorType = ColorType::RGBA_8888;
    srcRowBytes = ImageInfo::GetBytesPerPixel(colorType) * static_cast<size_t>(width());
    dstImageInfo = dstInfo.makeColorType(colorType);
    auto dstRowBytes = dstImageInfo.rowBytes();
    if (dstRowBytes % 16) {
      dstRowBytes = dstImageInfo.rowBytes() + GetPaddingAlignment16(dstImageInfo.rowBytes());
      dstImageInfo = ImageInfo::Make(dstInfo.width(), dstInfo.height(), colorType,
                                     dstInfo.alphaType(), dstRowBytes);
    }
    if (!dstTempBuffer.alloc(dstImageInfo.byteSize())) {
      return false;
    }
    dstData = dstTempBuffer.bytes();
  }
  srcRowBytes = srcRowBytes + GetPaddingAlignment16(srcRowBytes);
  if (!buffer.alloc(srcRowBytes * static_cast<size_t>(height()))) {
    return false;
  }
  auto result = onReadPixels(colorType, dstInfo.alphaType(), srcRowBytes, buffer.data());
  if (!result) {
    return false;
  }
  auto isOneComponent = dstImageInfo.colorType() == ColorType::Gray_8 ||
                        dstImageInfo.colorType() == ColorType::ALPHA_8;
  auto inputLayout = PixelLayout{width(), height(), static_cast<int>(srcRowBytes)};
  auto outputLayout = PixelLayout{dstImageInfo.width(), dstImageInfo.height(),
                                  static_cast<int>(dstImageInfo.rowBytes())};
  BoxFilterDownsample(buffer.data(), inputLayout, dstData, outputLayout, isOneComponent);
  if (!dstTempBuffer.isEmpty()) {
    Pixmap(dstImageInfo, dstData).readPixels(dstInfo, dstPixels);
  }
  return true;
}

std::shared_ptr<ImageBuffer> ImageCodec::onMakeBuffer(bool tryHardware) const {
  auto pixelBuffer =
      PixelBuffer::Make(width(), height(), isAlphaOnly(), tryHardware, colorSpace());
  if (pixelBuffer == nullptr) {
    return nullptr;
  }
  auto pixels = pixelBuffer->lockPixels();
  auto result = readPixels(pixelBuffer->info(), pixels);
  pixelBuffer->unlockPixels();
  return result ? pixelBuffer : nullptr;
}
}  // namespace tgfx
