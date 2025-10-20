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

#include "NV12HardwareBuffer.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/USE.h"
#include "gpu/resources/TextureView.h"

namespace tgfx {
static std::mutex cacheLocker = {};
static std::unordered_map<CVPixelBufferRef, std::weak_ptr<NV12HardwareBuffer>> nv12BufferMap = {};

std::shared_ptr<NV12HardwareBuffer> NV12HardwareBuffer::MakeFrom(CVPixelBufferRef pixelBuffer,
                                                                 YUVColorSpace colorSpace) {
  if (!HardwareBufferCheck(pixelBuffer)) {
    return nullptr;
  }
  auto format = CVPixelBufferGetPixelFormatType(pixelBuffer);
  if (format != kCVPixelFormatType_420YpCbCr8BiPlanarFullRange &&
      format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
    return nullptr;
  }
  std::lock_guard<std::mutex> cacheLock(cacheLocker);
  auto result = nv12BufferMap.find(pixelBuffer);
  if (result != nv12BufferMap.end()) {
    auto buffer = result->second.lock();
    if (buffer) {
      return buffer;
    }
    nv12BufferMap.erase(result);
  }
  auto buffer =
      std::shared_ptr<NV12HardwareBuffer>(new NV12HardwareBuffer(pixelBuffer, colorSpace));
  nv12BufferMap[pixelBuffer] = buffer;
  return buffer;
}

NV12HardwareBuffer::NV12HardwareBuffer(CVPixelBufferRef pixelBuffer, YUVColorSpace colorSpace)
    : pixelBuffer(pixelBuffer), colorSpace(colorSpace) {
  CFRetain(pixelBuffer);
}

NV12HardwareBuffer::~NV12HardwareBuffer() {
  CFRelease(pixelBuffer);
  std::lock_guard<std::mutex> cacheLock(cacheLocker);
  nv12BufferMap.erase(pixelBuffer);
}

int NV12HardwareBuffer::width() const {
  return static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
}

int NV12HardwareBuffer::height() const {
  return static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
}

std::shared_ptr<ColorSpace> NV12HardwareBuffer::gamutColorSpace() const {
  return MakeColorSpaceFromYUVColorSpace(colorSpace);
}

std::shared_ptr<TextureView> NV12HardwareBuffer::onMakeTexture(Context* context, bool) const {
  return TextureView::MakeFrom(context, pixelBuffer, colorSpace);
}
}  // namespace tgfx
