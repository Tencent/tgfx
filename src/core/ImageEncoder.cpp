/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/core/ImageEncoder.h"
#include <utility>
#include "core/codecs/ScanlineEncoder.h"
#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/WriteStream.h"

#ifdef TGFX_USE_PNG_ENCODE
#include "core/codecs/png/PngCodec.h"
#endif
#ifdef TGFX_USE_JPEG_ENCODE
#include "core/codecs/jpeg/JpegCodec.h"
#endif

namespace tgfx {

std::unique_ptr<ScanlineEncoder> ScanlineEncoder::Make(WriteStream* stream, const ImageInfo& info,
                                                       EncodedFormat format, int quality) {
#ifdef TGFX_USE_PNG_ENCODE
  if (format == EncodedFormat::PNG) {
    return PngCodec::MakeEncoder(stream, info);
  }
#endif
#ifdef TGFX_USE_JPEG_ENCODE
  if (format == EncodedFormat::JPEG) {
    return JpegCodec::MakeEncoder(stream, info, quality);
  }
#else
  static_cast<void>(quality);
#endif
#if !defined(TGFX_USE_PNG_ENCODE) && !defined(TGFX_USE_JPEG_ENCODE)
  static_cast<void>(stream);
  static_cast<void>(info);
  static_cast<void>(format);
#endif
  return nullptr;
}

class ImageEncoder::Impl {
 public:
  Impl(std::shared_ptr<WriteStream> destination, const ImageInfo& info,
       std::unique_ptr<ScanlineEncoder> encoder)
      : stream(std::move(destination)), info(info), encoder(std::move(encoder)) {
  }

  bool appendRows(const Pixmap& band) {
    if (!encoder) {
      return false;
    }
    const auto& bandInfo = band.info();
    if (band.isEmpty() || band.width() != info.width() || band.height() > info.height() - rows ||
        bandInfo.colorType() != info.colorType() || bandInfo.alphaType() != info.alphaType() ||
        !ColorSpace::Equals(bandInfo.colorSpace().get(), info.colorSpace().get()) ||
        !encoder->appendRows(band)) {
      encoder.reset();
      return false;
    }
    rows += band.height();
    return true;
  }

  bool finish() {
    auto activeEncoder = std::move(encoder);
    return activeEncoder && rows == info.height() && activeEncoder->finish();
  }

 private:
  std::shared_ptr<WriteStream> stream;
  ImageInfo info;
  std::unique_ptr<ScanlineEncoder> encoder;
  int rows = 0;
};

std::unique_ptr<ImageEncoder> ImageEncoder::Make(std::shared_ptr<WriteStream> destination,
                                                 const ImageInfo& imageInfo, EncodedFormat format,
                                                 int quality) {
  if (!destination || imageInfo.isEmpty() ||
      (imageInfo.colorType() != ColorType::RGBA_8888 &&
       imageInfo.colorType() != ColorType::BGRA_8888) ||
      (format == EncodedFormat::JPEG && imageInfo.alphaType() != AlphaType::Opaque)) {
    return nullptr;
  }
  auto encoder = ScanlineEncoder::Make(destination.get(), imageInfo, format, quality);
  if (!encoder) {
    return nullptr;
  }
  auto impl = std::make_unique<Impl>(std::move(destination), imageInfo, std::move(encoder));
  return std::unique_ptr<ImageEncoder>(new ImageEncoder(std::move(impl)));
}

ImageEncoder::ImageEncoder(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {
}

ImageEncoder::~ImageEncoder() = default;

bool ImageEncoder::appendRows(const Pixmap& band) {
  return impl->appendRows(band);
}

bool ImageEncoder::finish() {
  return impl->finish();
}

}  // namespace tgfx
