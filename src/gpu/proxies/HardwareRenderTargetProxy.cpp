/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "HardwareRenderTargetProxy.h"

namespace tgfx {
HardwareRenderTargetProxy::HardwareRenderTargetProxy(HardwareBufferRef hardwareBuffer, int width,
                                                     int height, PixelFormat format,
                                                     int sampleCount,
                                                     std::shared_ptr<ColorSpace> colorSpace)
    : TextureRenderTargetProxy(width, height, format, sampleCount, false, ImageOrigin::TopLeft,
                               true, std::move(colorSpace)),
      hardwareBuffer(hardwareBuffer) {
  HardwareBufferRetain(hardwareBuffer);
}

HardwareRenderTargetProxy::~HardwareRenderTargetProxy() {
  HardwareBufferRelease(hardwareBuffer);
}

std::shared_ptr<TextureView> HardwareRenderTargetProxy::onMakeTexture(Context* context) const {
  auto renderTarget =
      RenderTarget::MakeFrom(context, hardwareBuffer, _sampleCount, _colorSpace);
  if (renderTarget == nullptr) {
    LOGE("HardwareRenderTargetProxy::onMakeTexture() Failed to create the render target!");
  }
  return renderTarget->asTextureView();
}

}  // namespace tgfx
