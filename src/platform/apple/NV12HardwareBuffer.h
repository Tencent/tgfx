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

#pragma once

#import <CoreVideo/CoreVideo.h>
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
class NV12HardwareBuffer : public ImageBuffer {
 public:
  static std::shared_ptr<NV12HardwareBuffer> MakeFrom(CVPixelBufferRef pixelBuffer,
                                                      YUVColorSpace colorSpace);

  ~NV12HardwareBuffer() override;

  int width() const override;

  int height() const override;

  bool isAlphaOnly() const final {
    return false;
  }

  std::shared_ptr<ColorSpace> colorSpace() const override;

 protected:
  std::shared_ptr<TextureView> onMakeTexture(Context* context, bool mipmapped) const override;

 private:
  CVPixelBufferRef pixelBuffer = nullptr;
  YUVColorSpace _colorSpace = YUVColorSpace::BT601_LIMITED;

  NV12HardwareBuffer(CVPixelBufferRef pixelBuffer, YUVColorSpace colorSpace);
};

}  // namespace tgfx
