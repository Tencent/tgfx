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

#include "TextureRenderTargetProxy.h"

namespace tgfx {
TextureRenderTargetProxy::TextureRenderTargetProxy(int width, int height, PixelFormat format,
                                                   int sampleCount, bool mipmapped,
                                                   ImageOrigin origin, bool externallyOwned,
                                                   std::shared_ptr<ColorSpace> colorSpace)
    : DefaultTextureProxy(width, height, format, mipmapped, origin), _sampleCount(sampleCount),
      _externallyOwned(externallyOwned), _colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<RenderTarget> TextureRenderTargetProxy::getRenderTarget() const {
  auto textureView = DefaultTextureProxy::getTextureView();
  return textureView ? textureView->asRenderTarget() : nullptr;
}

std::shared_ptr<TextureView> TextureRenderTargetProxy::onMakeTexture(Context* context) const {
  if (_externallyOwned) {
    return nullptr;
  }
  auto renderTarget = RenderTarget::Make(context, _backingStoreWidth, _backingStoreHeight, _format,
                                         _sampleCount, _mipmapped, _origin);
  if (renderTarget == nullptr) {
    LOGE("TextureRenderTargetProxy::onMakeTexture() Failed to create the render target!");
    return nullptr;
  }
  return renderTarget->asTextureView();
}
}  // namespace tgfx
