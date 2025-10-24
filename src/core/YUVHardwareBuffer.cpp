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

#include "YUVHardwareBuffer.h"
#include "core/utils/USE.h"
#include "gpu/resources/TextureView.h"
#include "utils/ColorSpaceHelper.h"
#if defined(__OHOS__)
#include <native_buffer/native_buffer.h>
#endif

namespace tgfx {
static std::mutex cacheLocker = {};
static std::unordered_map<HardwareBufferRef, std::weak_ptr<YUVHardwareBuffer>> nv12BufferMap = {};

std::shared_ptr<YUVHardwareBuffer> YUVHardwareBuffer::MakeFrom(HardwareBufferRef hardwareBuffer,
                                                               YUVColorSpace colorSpace) {
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.format != HardwareBufferFormat::YCBCR_420_SP) {
    return nullptr;
  }
  std::lock_guard<std::mutex> cacheLock(cacheLocker);
  auto result = nv12BufferMap.find(hardwareBuffer);
  if (result != nv12BufferMap.end()) {
    auto buffer = result->second.lock();
    if (buffer) {
      return buffer;
    }
    nv12BufferMap.erase(result);
  }
#if defined(__OHOS__)
  // On the HarmonyOS platform, video-decoded HardwareBuffers may not have the correct color space
  // set, so we need to set it manually before creating the texture.
  switch (colorSpace) {
    case YUVColorSpace::BT601_LIMITED:
      OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT601_SMPTE_C_LIMIT);
      break;
    case YUVColorSpace::BT601_FULL:
      OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT601_SMPTE_C_FULL);
      break;
    case YUVColorSpace::BT709_LIMITED:
      OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT709_LIMIT);
      break;
    case YUVColorSpace::BT709_FULL:
      OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT709_FULL);
      break;
    case YUVColorSpace::BT2020_LIMITED:
      OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT2020_PQ_LIMIT);
      break;
    case YUVColorSpace::BT2020_FULL:
      OH_NativeBuffer_SetColorSpace(hardwareBuffer, OH_COLORSPACE_BT2020_PQ_FULL);
      break;
    default:
      break;
  }
#endif
  auto buffer = std::shared_ptr<YUVHardwareBuffer>(
      new YUVHardwareBuffer(info.width, info.height, hardwareBuffer, colorSpace));
  nv12BufferMap[hardwareBuffer] = buffer;
  return buffer;
}

YUVHardwareBuffer::YUVHardwareBuffer(int width, int height, HardwareBufferRef hardwareBuffer,
                                     YUVColorSpace colorSpace)
    : _width(width), _height(height), hardwareBuffer(hardwareBuffer), _yuvColorSpace(colorSpace) {
  HardwareBufferRetain(hardwareBuffer);
}

YUVHardwareBuffer::~YUVHardwareBuffer() {
  HardwareBufferRelease(hardwareBuffer);
  std::lock_guard<std::mutex> cacheLock(cacheLocker);
  nv12BufferMap.erase(hardwareBuffer);
}

std::shared_ptr<ColorSpace> YUVHardwareBuffer::colorSpace() const {
  if (_colorSpace == nullptr) {
    _colorSpace = MakeColorSpaceFromYUVColorSpace(_yuvColorSpace);
  }
  return _colorSpace;
}

std::shared_ptr<TextureView> YUVHardwareBuffer::onMakeTexture(Context* context, bool) const {
  return TextureView::MakeFrom(context, hardwareBuffer, _yuvColorSpace);
}
}  // namespace tgfx
