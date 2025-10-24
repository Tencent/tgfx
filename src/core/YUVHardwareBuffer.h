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

#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
/**
 * YUVHardwareBuffer represents a pixel array in the YUV format, stored in a platform-specific
 * hardware buffer.
 */
class YUVHardwareBuffer : public ImageBuffer {
 public:
  static std::shared_ptr<YUVHardwareBuffer> MakeFrom(HardwareBufferRef hardwareBuffer,
                                                     YUVColorSpace colorSpace);

  ~YUVHardwareBuffer() override;

  int width() const override {
    return _width;
  }

  int height() const override {
    return _height;
  }

  bool isAlphaOnly() const final {
    return false;
  }

  std::shared_ptr<ColorSpace> colorSpace() const override;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override;

 private:
  int _width = 0;
  int _height = 0;
  HardwareBufferRef hardwareBuffer = nullptr;
  mutable std::shared_ptr<ColorSpace> _colorSpace = nullptr;
  YUVColorSpace _yuvColorSpace = YUVColorSpace::BT601_LIMITED;

  YUVHardwareBuffer(int width, int height, HardwareBufferRef hardwareBuffer,
                    YUVColorSpace colorSpace);
};

}  // namespace tgfx
