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

std::vector<PixelFormat> EGLGPU::getHardwareTextureFormats(HardwareBufferRef hardwareBuffer,
                                                           YUVFormat* yuvFormat) const {
  static const auto describe = AHardwareBufferFunctions::Get()->describe;
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  std::vector<PixelFormat> formats = {};
  AHardwareBuffer_Desc desc;
  describe(hardwareBuffer, &desc);
  switch (desc.format) {
    case HARDWAREBUFFER_FORMAT_R8_UNORM:
      formats.push_back(PixelFormat::ALPHA_8);
      break;
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      formats.push_back(PixelFormat::RGBA_8888);
      break;
    default:
      break;
  }
  if (yuvFormat != nullptr) {
    *yuvFormat = YUVFormat::Unknown;
  }
  return formats;
}

#elif defined(__OHOS__)

bool HardwareBufferAvailable() {
  return true;
}

std::vector<PixelFormat> EGLGPU::getHardwareTextureFormats(HardwareBufferRef hardwareBuffer,
                                                           YUVFormat* yuvFormat) const {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  if (yuvFormat != nullptr) {
    *yuvFormat = YUVFormat::Unknown;
  }
  std::vector<PixelFormat> formats = {};
  OH_NativeBuffer_Config config;
  OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
  switch (config.format) {
    case NATIVEBUFFER_PIXEL_FMT_RGBA_8888:
    case NATIVEBUFFER_PIXEL_FMT_RGBX_8888:
      formats.push_back(PixelFormat::RGBA_8888);
      break;
    case NATIVEBUFFER_PIXEL_FMT_YCBCR_420_SP:
    case NATIVEBUFFER_PIXEL_FMT_YCRCB_420_SP:
      formats.push_back(PixelFormat::RGBA_8888);
      if (yuvFormat != nullptr) {
        *yuvFormat = YUVFormat::NV12;
      }
      break;
    case NATIVEBUFFER_PIXEL_FMT_YCBCR_420_P:
    case NATIVEBUFFER_PIXEL_FMT_YCRCB_420_P:
      formats.push_back(PixelFormat::RGBA_8888);
      if (yuvFormat != nullptr) {
        *yuvFormat = YUVFormat::I420;
      }
      break;
    default:
      break;
  }
  return formats;
}

#else

bool HardwareBufferAvailable() {
  return false;
}

std::vector<PixelFormat> EGLGPU::getHardwareTextureFormats(HardwareBufferRef, YUVFormat*) const {
  return {};
}

#endif

#if defined(__ANDROID__) || defined(ANDROID) || defined(__OHOS__)

std::vector<std::shared_ptr<GPUTexture>> EGLGPU::importHardwareTextures(
    HardwareBufferRef hardwareBuffer, uint32_t usage) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto texture = EGLHardwareTexture::MakeFrom(this, hardwareBuffer, usage);
  if (texture == nullptr) {
    return {};
  }
  std::vector<std::shared_ptr<GPUTexture>> textures = {};
  textures.push_back(std::move(texture));
  return textures;
}

#else

std::vector<std::shared_ptr<GPUTexture>> EGLGPU::importHardwareTextures(HardwareBufferRef,
                                                                        uint32_t) {
  return {};
}

#endif

}  // namespace tgfx
