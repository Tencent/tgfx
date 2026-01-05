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

#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#endif

#if defined(_WIN32) && (QT_VERSION_MAJOR < 6)
// For Qt5 on Windows, combaseapi.h is included, in which interface is defined as struct FAR.
#ifdef interface
#undef interface
#endif
#endif
#include "gpu/opengl/GLGPU.h"

namespace tgfx {
class QGLGPU : public GLGPU {
 public:
  explicit QGLGPU(std::shared_ptr<GLInterface> glInterface) : GLGPU(std::move(glInterface)) {
  }

#ifdef __APPLE__
  ~QGLGPU() override;
#endif

  std::vector<std::shared_ptr<Texture>> importHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                               uint32_t usage) override;

 private:
#ifdef __APPLE__
  CVOpenGLTextureCacheRef textureCache = nil;
  CVOpenGLTextureCacheRef getTextureCache();
#endif
};
}  // namespace tgfx
