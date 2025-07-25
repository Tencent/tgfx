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

#include "tgfx/core/ImageCodec.h"

namespace tgfx {
class RawPixelCodec : public ImageCodec {
 public:
  RawPixelCodec(const ImageInfo& info, std::shared_ptr<Data> pixels)
      : ImageCodec(info.width(), info.height()), info(info), pixels(std::move(pixels)) {
  }

  bool isAlphaOnly() const override {
    return info.isAlphaOnly();
  }

  bool onReadPixels(const ImageInfo& dstInfo, void* dstPixels) const override {
    return Pixmap(info, pixels->data()).readPixels(dstInfo, dstPixels);
  }

 protected:
  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override;

 private:
  ImageInfo info = {};
  std::shared_ptr<Data> pixels = nullptr;
};
}  // namespace tgfx
