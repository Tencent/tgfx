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

#include "CGLHardwareTexture.h"
#include "tgfx/gpu/opengl/cgl/CGLDevice.h"

namespace tgfx {
bool HardwareBufferAvailable() {
  return true;
}

std::shared_ptr<Texture> Texture::MakeFrom(Context* context, HardwareBufferRef hardwareBuffer,
                                           YUVColorSpace) {
  if (!HardwareBufferCheck(hardwareBuffer)) {
    return nullptr;
  }
  auto cglDevice = static_cast<CGLDevice*>(context->device());
  if (cglDevice == nullptr) {
    return nullptr;
  }
  auto textureCache = cglDevice->getTextureCache();
  return CGLHardwareTexture::MakeFrom(context, hardwareBuffer, textureCache);
}
}  // namespace tgfx
