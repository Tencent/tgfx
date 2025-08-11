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

#include "gpu/opengl/GLGPU.h"

namespace tgfx {
class EGLGPU : public GLGPU {
 public:
  EGLGPU(std::shared_ptr<GLInterface> glInterface, void* eglDisplay)
      : GLGPU(std::move(glInterface)), eglDisplay(eglDisplay) {
  }

  void* getDisplay() const {
    return eglDisplay;
  }

  PixelFormat getPixelFormat(HardwareBufferRef hardwareBuffer) const override;

  std::vector<std::unique_ptr<GPUTexture>> createHardwareTextures(HardwareBufferRef hardwareBuffer,
                                                                  YUVFormat* yuvFormat) override;

 private:
  void* eglDisplay = nullptr;
};
}  // namespace tgfx
