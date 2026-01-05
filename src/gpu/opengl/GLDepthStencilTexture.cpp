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

#include "GLDepthStencilTexture.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
std::shared_ptr<GLDepthStencilTexture> GLDepthStencilTexture::MakeFrom(
    GLGPU* gpu, const TextureDescriptor& descriptor) {
  DEBUG_ASSERT(gpu != nullptr);
  DEBUG_ASSERT(descriptor.format == PixelFormat::DEPTH24_STENCIL8);
  if (!(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    LOGE("GLDepthStencilTexture::MakeFrom() usage does not include RENDER_ATTACHMENT!");
    return nullptr;
  }
  if (descriptor.usage & TextureUsage::TEXTURE_BINDING) {
    LOGE("GLDepthStencilTexture::MakeFrom() usage includes TEXTURE_BINDING!");
    return nullptr;
  }
  if (descriptor.mipLevelCount > 1) {
    LOGE("GLDepthStencilTexture::MakeFrom() mipLevelCount should be 1 for depthStencil textures!");
    return nullptr;
  }

  unsigned renderBufferID = 0;
  auto gl = gpu->functions();
  ClearGLError(gl);
  gl->genRenderbuffers(1, &renderBufferID);
  if (renderBufferID == 0) {
    LOGE("GLDepthStencilTexture::MakeFrom() failed to generate renderbuffer!");
    return nullptr;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, renderBufferID);
  gl->renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, descriptor.width,
                          descriptor.height);
  if (!CheckGLError(gl)) {
    gl->deleteRenderbuffers(1, &renderBufferID);
    return nullptr;
  }
  return gpu->makeResource<GLDepthStencilTexture>(descriptor, renderBufferID);
}

void GLDepthStencilTexture::onReleaseTexture(GLGPU* gpu) {
  if (_renderBufferID > 0) {
    auto gl = gpu->functions();
    gl->deleteRenderbuffers(1, &_renderBufferID);
    _renderBufferID = 0;
  }
}
}  // namespace tgfx
