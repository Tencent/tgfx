/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "core/utils/WeakMap.h"
#include "gpu/Texture.h"

namespace tgfx {
static WeakMap<OH_NativeBuffer*, ExternalOESBuffer> oesBufferMap = {};

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

  auto buffer = oesBufferMap.find(hardwareBuffer);
  if (buffer) {
    return buffer;
  }
  buffer = std::shared_ptr<ExternalOESBuffer>(new ExternalOESBuffer(hardwareBuffer, colorSpace));
  oesBufferMap.insert(hardwareBuffer, buffer);
  return buffer;
}

ExternalOESBuffer::ExternalOESBuffer(OH_NativeBuffer* hardwareBuffer, YUVColorSpace colorSpace)
    : hardwareBuffer(hardwareBuffer), colorSpace(colorSpace) {
  OH_NativeBuffer_Reference(hardwareBuffer);
}

ExternalOESBuffer::~ExternalOESBuffer() {
  OH_NativeBuffer_Unreference(hardwareBuffer);
  oesBufferMap.remove(hardwareBuffer);
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

std::shared_ptr<Texture> ExternalOESBuffer::onMakeTexture(Context* context, bool) const {
  return Texture::MakeFrom(context, hardwareBuffer, colorSpace);
}
}  // namespace tgfx
