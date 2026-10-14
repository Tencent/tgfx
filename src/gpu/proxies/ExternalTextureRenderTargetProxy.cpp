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

#include "ExternalTextureRenderTargetProxy.h"
#include "gpu/opengl/GLDefines.h"
#include "tgfx/gpu/opengl/GLTypes.h"

namespace tgfx {
ExternalTextureRenderTargetProxy::ExternalTextureRenderTargetProxy(
    const BackendTexture& backendTexture, PixelFormat format, int sampleCount, ImageOrigin origin,
    bool adopted)
    : TextureRenderTargetProxy(backendTexture.width(), backendTexture.height(), format, sampleCount,
                               false, origin, !adopted),
      backendTexture(backendTexture) {
  // Only OpenGL can hand over a texture whose target is not 2D (GL_TEXTURE_RECTANGLE, or
  // GL_TEXTURE_EXTERNAL_OES). Every other backend's textures are sampled as 2D.
  GLTextureInfo glInfo = {};
  if (backendTexture.getGLTextureInfo(&glInfo)) {
    sampledDirectly = glInfo.target == GL_TEXTURE_2D;
  }
}

std::shared_ptr<TextureView> ExternalTextureRenderTargetProxy::onMakeTexture(
    Context* context) const {
  auto renderTarget =
      RenderTarget::MakeFrom(context, backendTexture, _sampleCount, _origin, !externallyOwned());
  if (renderTarget == nullptr) {
    LOGE("BackendTextureRenderTargetProxy::onMakeTexture() Failed to create the render target!");
    return nullptr;
  }
  return renderTarget->asTextureView();
}
}  // namespace tgfx
