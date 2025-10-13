/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "ExternalOESBuffer.h"
#include <mutex>
#include "gpu/resources/TextureView.h"

namespace tgfx {
static std::mutex cacheLocker = {};
static std::unordered_map<OH_NativeBuffer*, std::weak_ptr<ExternalOESBuffer>> oesBufferMap = {};

std::shared_ptr<ExternalOESBuffer> ExternalOESBuffer::MakeFrom(OH_NativeBuffer* hardwareBuffer,
                                                               YUVColorSpace colorSpace) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  OH_NativeBuffer_Config config;
  OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
  if (config.format < NATIVEBUFFER_PIXEL_FMT_YUV_422_I ||
      config.format > NATIVEBUFFER_PIXEL_FMT_YCRCB_P010) {
    return nullptr;
  }
  std::lock_guard<std::mutex> cacheLock(cacheLocker);
  auto result = oesBufferMap.find(hardwareBuffer);
  if (result != oesBufferMap.end()) {
    auto buffer = result->second.lock();
    if (buffer) {
      return buffer;
    }
    oesBufferMap.erase(result);
  }
  auto buffer =
      std::shared_ptr<ExternalOESBuffer>(new ExternalOESBuffer(hardwareBuffer, colorSpace));
  oesBufferMap[hardwareBuffer] = buffer;
  return buffer;
}

ExternalOESBuffer::ExternalOESBuffer(OH_NativeBuffer* hardwareBuffer, YUVColorSpace colorSpace)
    : hardwareBuffer(hardwareBuffer), colorSpace(colorSpace) {
  OH_NativeBuffer_Reference(hardwareBuffer);
}

ExternalOESBuffer::~ExternalOESBuffer() {
  OH_NativeBuffer_Unreference(hardwareBuffer);
  std::lock_guard<std::mutex> cacheLock(cacheLocker);
  oesBufferMap.erase(hardwareBuffer);
}

int ExternalOESBuffer::width() const {
  OH_NativeBuffer_Config config;
  OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
  return config.width;
}

int ExternalOESBuffer::height() const {
  OH_NativeBuffer_Config config;
  OH_NativeBuffer_GetConfig(hardwareBuffer, &config);
  return config.height;
}

std::shared_ptr<TextureView> ExternalOESBuffer::onMakeTexture(Context* context, bool) const {
  return TextureView::MakeFrom(context, hardwareBuffer, colorSpace);
}
}  // namespace tgfx
