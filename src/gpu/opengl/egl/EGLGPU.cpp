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

PixelFormat EGLGPU::getPixelFormat(HardwareBufferRef) const {
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.isEmpty()) {
    return PixelFormat::Unknown;
  }
  return ColorTypeToPixelFormat(info.colorType());
}

std::vector<std::unique_ptr<GPUTexture>> EGLGPU::createHardwareTextures(
    HardwareBufferRef hardwareBuffer, YUVFormat* yuvFormat) const {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto texture = EGLHardwareTexture::MakeFrom(this, hardwareBuffer);
  if (texture == nullptr) {
    return {};
  }
  if (yuvFormat != nullptr) {
    *yuvFormat = YUVFormat::Unknown;
  }
  std::vector<std::unique_ptr<GPUTexture>> textures = {};
  textures.push_back(std::move(texture));
  return textures;
}

#else

PixelFormat EGLGPU::getPixelFormat(HardwareBufferRef) const {
  return PixelFormat::Unknown;
}

std::vector<std::unique_ptr<GPUTexture>> EGLGPU::createHardwareTextures(HardwareBufferRef,
                                                                        YUVFormat*) const {
  return {};
}

#endif
}  // namespace tgfx
