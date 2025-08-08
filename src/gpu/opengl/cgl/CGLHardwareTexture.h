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

#include <CoreVideo/CoreVideo.h>
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
class CGLHardwareTexture : public GLTexture {
 public:
  static std::unique_ptr<CGLHardwareTexture> MakeFrom(CVPixelBufferRef pixelBuffer,
                                                      CVOpenGLTextureCacheRef textureCache);

  ~CGLHardwareTexture() override;

  HardwareBufferRef getHardwareBuffer() const override {
    return pixelBuffer;
  }

  void releaseGPU(Context*) override;

 private:
  CVPixelBufferRef pixelBuffer = nullptr;
  CVOpenGLTextureRef texture = nil;
  CVOpenGLTextureCacheRef textureCache = nil;

  CGLHardwareTexture(CVPixelBufferRef pixelBuffer, CVOpenGLTextureCacheRef textureCache,
                     unsigned id, unsigned target, PixelFormat format);
};
}  // namespace tgfx
