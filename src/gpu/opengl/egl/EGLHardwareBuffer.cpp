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

#include "EGLHardwareTexture.h"
#include "core/utils/USE.h"
#include "gpu/Texture.h"
#include "tgfx/platform/HardwareBuffer.h"

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

std::shared_ptr<Texture> Texture::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           YUVColorSpace) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  return EGLHardwareTexture::MakeFrom(context, hardwareBuffer);
}

#elif defined(__OHOS__)

bool HardwareBufferAvailable() {
  return true;
}

std::shared_ptr<Texture> Texture::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           YUVColorSpace colorSpace) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
#if defined(__OHOS__)
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
#else
  USE(colorSpace);
#endif
  return EGLHardwareTexture::MakeFrom(context, hardwareBuffer);
}

#else

bool HardwareBufferAvailable() {
  return false;
}

std::shared_ptr<Texture> Texture::MakeFrom(Context*, HardwareBufferRef, YUVColorSpace) {
  return nullptr;
}

#endif
}  // namespace tgfx