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
#pragma once

#include "PixelBuffer.h"
#include "tgfx/core/ImageCodec.h"

namespace tgfx {
class PixelBufferCodec : public ImageCodec {
 public:
  static std::shared_ptr<PixelBufferCodec> Make(std::shared_ptr<PixelBuffer> source);

  PixelBufferCodec(std::shared_ptr<PixelBuffer> source)
      : ImageCodec(source->width(), source->height(), Orientation::TopLeft, source->colorSpace()),
        source(std::move(source)) {
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                    std::shared_ptr<ColorSpace> dstColorSpace, void* dstPixels) const override;

 private:
  std::shared_ptr<PixelBuffer> source = nullptr;
};
}  // namespace tgfx