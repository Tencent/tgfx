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

#include "gpu/TextureView.h"
#include "tgfx/gpu/opengl/qt/QGLDevice.h"
#ifdef __APPLE__
#include "gpu/opengl/cgl/CGLHardwareTexture.h"
#endif
#include "tgfx/platform/HardwareBuffer.h"

namespace tgfx {
#ifdef __APPLE__

bool HardwareBufferAvailable() {
  return true;
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
      return PixelFormat::RGBA_8888;
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
  auto textureCache = static_cast<QGLDevice*>(context->device())->getTextureCache();
  auto texture = CGLHardwareTexture::MakeFrom(hardwareBuffer, textureCache);
  if (texture == nullptr) {
    return {};
  }
  if (yuvFormat != nullptr) {
    *yuvFormat = YUVFormat::Unknown;
  }
  std::vector<std::unique_ptr<GPUTexture>> textures = {};
  textures.push_back(std::move(texture));
  return textures;
}

#else

bool HardwareBufferAvailable() {
  return false;
}

PixelFormat GPUTexture::GetPixelFormat(HardwareBufferRef) {
  return PixelFormat::Unknown;
}

std::vector<std::unique_ptr<GPUTexture>> GPUTexture::MakeFrom(Context*, HardwareBufferRef,
                                                              YUVFormat*) {
  return {};
}

#endif
}  // namespace tgfx