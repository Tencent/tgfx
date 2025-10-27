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

#include <AppKit/AppKit.h>
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
class CGLGPU : public GLGPU {
 public:
  explicit CGLGPU(std::shared_ptr<GLInterface> glInterface, CGLContextObj cglContext)
      : GLGPU(std::move(glInterface)), cglContext(cglContext) {
  }

  ~CGLGPU() override;

  CVOpenGLTextureCacheRef getTextureCache();

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                               uint32_t usage) override;

 private:
  CGLContextObj cglContext = nil;
  CVOpenGLTextureCacheRef textureCache = nil;
};
}  // namespace tgfx
