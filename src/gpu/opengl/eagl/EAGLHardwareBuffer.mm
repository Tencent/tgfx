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

#import <TargetConditionals.h>
#include "EAGLHardwareTexture.h"
#include "core/PixelBuffer.h"
#include "platform/apple/NV12HardwareBuffer.h"

namespace tgfx {
bool HardwareBufferAvailable() {
#if TARGET_IPHONE_SIMULATOR
  return false;
#else
  return true;
#endif
}

PixelFormat GPUTexture::GetPixelFormat(HardwareBufferRef hardwareBuffer) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return PixelFormat::Unknown;
  }
  auto pixelFormat = CVPixelBufferGetPixelFormatType(hardwareBuffer);
  switch (pixelFormat) {
    case kCVPixelFormatType_OneComponent8:
      return PixelFormat::ALPHA_8;
    case kCVPixelFormatType_32BGRA:
      return PixelFormat::BGRA_8888;
    default:
      return PixelFormat::Unknown;
  }
}

std::vector<std::unique_ptr<GPUTexture>> GPUTexture::MakeFrom(Context* context,
                                                              HardwareBufferRef hardwareBuffer,
                                                              YUVFormat* yuvFormat) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto textures = EAGLHardwareTexture::MakeFrom(context, hardwareBuffer);
  if (yuvFormat != nullptr && !textures.empty()) {
    *yuvFormat = textures.size() == 2 ? YUVFormat::NV12 : YUVFormat::Unknown;
  }
  return textures;
}
}  // namespace tgfx
