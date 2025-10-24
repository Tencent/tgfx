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

#include "tgfx/platform/HardwareBuffer.h"
#include <native_buffer/native_buffer.h>
#include "core/utils/PixelFormatUtil.h"

namespace tgfx {
#define BUFFER_USAGE_MEM_MMZ_CACHE (1ULL << 5)

bool HardwareBufferCheck(HardwareBufferRef buffer) {
  if (!HardwareBufferAvailable()) {
    return false;
  }
  return buffer != nullptr;
}

HardwareBufferRef HardwareBufferAllocate(int width, int height, bool alphaOnly) {
  if (!HardwareBufferAvailable() || alphaOnly) {
    return nullptr;
  }
  OH_NativeBuffer_Config config = {width, height, NATIVEBUFFER_PIXEL_FMT_RGBA_8888,
                                   NATIVEBUFFER_USAGE_CPU_READ | BUFFER_USAGE_MEM_MMZ_CACHE |
                                       NATIVEBUFFER_USAGE_CPU_WRITE | NATIVEBUFFER_USAGE_HW_RENDER |
                                       NATIVEBUFFER_USAGE_HW_TEXTURE,
                                   0};
  return OH_NativeBuffer_Alloc(&config);
}

HardwareBufferRef HardwareBufferRetain(HardwareBufferRef buffer) {
  if (buffer != nullptr) {
    OH_NativeBuffer_Reference(buffer);
  }
  return buffer;
}

void HardwareBufferRelease(HardwareBufferRef buffer) {
  if (buffer != nullptr) {
    OH_NativeBuffer_Unreference(buffer);
  }
}

void* HardwareBufferLock(HardwareBufferRef buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  void* pixels = nullptr;
  auto result = OH_NativeBuffer_Map(buffer, &pixels);
  if (result != 0) {
    return nullptr;
  }
  return pixels;
}

void HardwareBufferUnlock(HardwareBufferRef buffer) {
  if (buffer != nullptr) {
    OH_NativeBuffer_Unmap(buffer);
  }
}

HardwareBufferInfo HardwareBufferGetInfo(HardwareBufferRef buffer) {
  if (!HardwareBufferAvailable() || buffer == nullptr) {
    return {};
  }
  OH_NativeBuffer_Config config;
  OH_NativeBuffer_GetConfig(buffer, &config);
  HardwareBufferInfo info = {};
  switch (config.format) {
    case NATIVEBUFFER_PIXEL_FMT_RGBA_8888:
    case NATIVEBUFFER_PIXEL_FMT_RGBX_8888:
      info.format = HardwareBufferFormat::RGBA_8888;
      break;
    case NATIVEBUFFER_PIXEL_FMT_YCBCR_420_SP:
      info.format = HardwareBufferFormat::YCBCR_420_SP;
      break;
    default:
      return {};
  }
  info.width = config.width;
  info.height = config.height;
  info.range = static_cast<size_t>(config.stride);
  return info;
}
}  // namespace tgfx
