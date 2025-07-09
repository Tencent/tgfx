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
#include "EAGLNV12Texture.h"
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

std::vector<PixelFormat> TextureSampler::GetFormats(HardwareBufferRef hardwareBuffer,
                                                    YUVFormat* yuvFormat) {
  std::vector<PixelFormat> formats = {};
  if (HardwareBufferCheck(hardwareBuffer)) {
    auto pixelFormat = CVPixelBufferGetPixelFormatType(hardwareBuffer);
    switch (pixelFormat) {
      case kCVPixelFormatType_OneComponent8:
        formats.push_back(PixelFormat::ALPHA_8);
        break;
      case kCVPixelFormatType_32BGRA:
        formats.push_back(PixelFormat::RGBA_8888);
        break;
      case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
      case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
        formats.push_back(PixelFormat::GRAY_8);
        formats.push_back(PixelFormat::RG_88);
        break;
      default:
        break;
    }
  }
  if (yuvFormat != nullptr) {
    *yuvFormat = formats.size() == 2 ? YUVFormat::NV12 : YUVFormat::Unknown;
  }
  return formats;
}

std::shared_ptr<Texture> Texture::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           YUVColorSpace colorSpace) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  auto planeCount = CVPixelBufferGetPlaneCount(hardwareBuffer);
  if (planeCount > 1) {
    return EAGLNV12Texture::MakeFrom(context, hardwareBuffer, colorSpace);
  }
  return EAGLHardwareTexture::MakeFrom(context, hardwareBuffer);
}
}  // namespace tgfx
