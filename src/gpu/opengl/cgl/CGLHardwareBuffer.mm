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

#include "CGLHardwareTextureSampler.h"
#include "tgfx/gpu/opengl/cgl/CGLDevice.h"

namespace tgfx {
bool HardwareBufferAvailable() {
  return true;
}

PixelFormat TextureSampler::GetRenderableFormat(HardwareBufferRef hardwareBuffer) {
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

std::vector<std::unique_ptr<TextureSampler>> TextureSampler::MakeFrom(
    Context* context, HardwareBufferRef hardwareBuffer, YUVFormat* yuvFormat) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return {};
  }
  auto textureCache = static_cast<CGLDevice*>(context->device())->getTextureCache();
  auto sampler = CGLHardwareTextureSampler::MakeFrom(hardwareBuffer, textureCache);
  if (sampler == nullptr) {
    return {};
  }
  if (yuvFormat != nullptr) {
    *yuvFormat = YUVFormat::Unknown;
  }
  std::vector<std::unique_ptr<TextureSampler>> samplers = {};
  samplers.push_back(std::move(sampler));
  return samplers;
}
}  // namespace tgfx
