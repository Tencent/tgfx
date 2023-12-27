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

#include "TextureRenderTargetProxy.h"

namespace tgfx {

TextureRenderTargetProxy::TextureRenderTargetProxy(std::shared_ptr<TextureProxy> textureProxy,
                                                   PixelFormat format, int sampleCount,
                                                   bool externallyOwned)
    : textureProxy(std::move(textureProxy)), _format(format), _sampleCount(sampleCount),
      _externallyOwned(externallyOwned) {
}

std::shared_ptr<RenderTarget> TextureRenderTargetProxy::onMakeRenderTarget() {
  if (!textureProxy->isInstantiated()) {
    textureProxy->instantiate();
  }
  auto texture = textureProxy->getTexture();
  return RenderTarget::MakeFrom(texture.get(), _sampleCount);
}
}  // namespace tgfx
