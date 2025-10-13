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

#include <CoreVideo/CoreVideo.h>
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
class EAGLGPU : public GLGPU {
 public:
  explicit EAGLGPU(std::shared_ptr<GLInterface> glInterface, EAGLContext* eaglContext)
      : GLGPU(std::move(glInterface)), _eaglContext(eaglContext) {
  }

  ~EAGLGPU() override;

  EAGLContext* eaglContext() const {
    return _eaglContext;
  }

  CVOpenGLESTextureCacheRef getTextureCache();

  std::vector<PixelFormat> getHardwareTextureFormats(HardwareBufferRef hardwareBuffer,
                                                     YUVFormat* yuvFormat) const override;

  std::vector<std::shared_ptr<GPUTexture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                                  uint32_t usage) override;

 private:
  EAGLContext* _eaglContext = nil;
  CVOpenGLESTextureCacheRef textureCache = nil;
};
}  // namespace tgfx
