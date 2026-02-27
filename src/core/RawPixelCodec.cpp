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

#include "RawPixelCodec.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {
std::shared_ptr<ImageCodec> ImageCodec::MakeFrom(const ImageInfo& info,
                                                 std::shared_ptr<Data> pixels) {
  if (info.isEmpty() || pixels == nullptr || info.byteSize() > pixels->size()) {
    return nullptr;
  }
  return std::make_shared<RawPixelCodec>(info, std::move(pixels));
}

class RawPixelData : public ImageBuffer {
 public:
  RawPixelData(const ImageInfo& info, std::shared_ptr<Data> pixels)
      : info(info), pixels(std::move(pixels)) {
  }

  int width() const override {
    return info.width();
  }

  int height() const override {
    return info.height();
  }

  bool isAlphaOnly() const override {
    return info.isAlphaOnly();
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return info.colorSpace();
  }

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override {
    switch (info.colorType()) {
      case ColorType::ALPHA_8:
        return TextureView::MakeAlpha(context, info.width(), info.height(), pixels->data(),
                                      info.rowBytes(), mipmapped);
      case ColorType::BGRA_8888:
        return TextureView::MakeFormat(context, info.width(), info.height(), pixels->data(),
                                       info.rowBytes(), PixelFormat::BGRA_8888, mipmapped);
      case ColorType::RGBA_8888:
        return TextureView::MakeRGBA(context, info.width(), info.height(), pixels->data(),
                                     info.rowBytes(), mipmapped);
      default:
        return nullptr;
    }
  }

 private:
  ImageInfo info = {};
  std::shared_ptr<Data> pixels = nullptr;
};

std::shared_ptr<ImageBuffer> RawPixelCodec::onMakeBuffer(bool tryHardware) const {
  if (info.alphaType() != AlphaType::Unpremultiplied) {
    switch (info.colorType()) {
      case ColorType::ALPHA_8:
      case ColorType::RGBA_8888:
      case ColorType::BGRA_8888:
        return std::make_shared<RawPixelData>(info, pixels);
      default:
        return nullptr;
    }
  }
  return ImageCodec::onMakeBuffer(tryHardware);
}

}  // namespace tgfx
