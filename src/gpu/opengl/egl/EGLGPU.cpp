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

#include "EGLGPU.h"
#include "EGLHardwareTexture.h"
#include "core/utils/PixelFormatUtil.h"

#if defined(__ANDROID__) || defined(ANDROID)
#include "platform/android/AHardwareBufferFunctions.h"
#elif defined(__OHOS__)
#include <native_buffer/native_buffer.h>
#endif

namespace tgfx {
#if defined(__ANDROID__) || defined(ANDROID)

bool HardwareBufferAvailable() {
  static const bool available = AHardwareBufferFunctions::Get()->allocate != nullptr &&
                                AHardwareBufferFunctions::Get()->release != nullptr &&
                                AHardwareBufferFunctions::Get()->lock != nullptr &&
                                AHardwareBufferFunctions::Get()->unlock != nullptr &&
                                AHardwareBufferFunctions::Get()->describe != nullptr &&
                                AHardwareBufferFunctions::Get()->acquire != nullptr &&
                                AHardwareBufferFunctions::Get()->toHardwareBuffer != nullptr &&
                                AHardwareBufferFunctions::Get()->fromHardwareBuffer != nullptr;
  return available;
}

#elif defined(__OHOS__)

bool HardwareBufferAvailable() {
  return true;
}

#else

bool HardwareBufferAvailable() {
  return false;
}

#endif

#if defined(__ANDROID__) || defined(ANDROID) || defined(__OHOS__)

std::vector<std::shared_ptr<Texture>> EGLGPU::importHardwareTextures(
    HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto texture = EGLHardwareTexture::MakeFrom(this, hardwareBuffer, usage);
  if (texture == nullptr) {
    return {};
  }
  std::vector<std::shared_ptr<Texture>> textures = {};
  textures.push_back(std::move(texture));
  return textures;
}

#else

std::vector<std::shared_ptr<Texture>> EGLGPU::importHardwareTextures(HardwareBufferRef, uint32_t) {
  return {};
}

#endif

}  // namespace tgfx
