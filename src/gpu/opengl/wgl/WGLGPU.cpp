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

#include "WGLGPU.h"
#include "WGLHardwareTexture.h"
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {

bool HardwareBufferAvailable() {
  return true;
}

std::vector<std::shared_ptr<Texture>> WGLGPU::importHardwareTextures(
    HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.format == HardwareBufferFormat::YCBCR_420_SP) {
    // NV12 textures should be converted to BGRA before reaching here.
    return {};
  }
  auto texture = WGLHardwareTexture::MakeFrom(this, hardwareBuffer, usage);
  if (texture == nullptr) {
    return {};
  }
  return {std::move(texture)};
}

}  // namespace tgfx
