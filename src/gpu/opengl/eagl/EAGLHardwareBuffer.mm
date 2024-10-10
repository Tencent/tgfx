/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
