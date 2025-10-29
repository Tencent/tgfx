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

#include "EAGLLayerTexture.h"
#include "core/utils/Log.h"
#include "gpu/opengl/GLUtil.h"
#include "gpu/opengl/eagl/EAGLGPU.h"

namespace tgfx {
std::unique_ptr<EAGLLayerTexture> EAGLLayerTexture::MakeFrom(GLGPU* gpu, CAEAGLLayer* layer) {
  DEBUG_ASSERT(gpu != nullptr);
  if (layer == nil) {
    return nullptr;
  }
  auto width = layer.bounds.size.width * layer.contentsScale;
  auto height = layer.bounds.size.height * layer.contentsScale;
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto gl = gpu->functions();
  ClearGLError(gl);
  unsigned frameBufferID = 0;
  gl->genFramebuffers(1, &frameBufferID);
  if (frameBufferID == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to generate framebuffer!");
    return nullptr;
  }
  TextureDescriptor descriptor = {
      static_cast<int>(width),        static_cast<int>(height), PixelFormat::RGBA_8888, false, 1,
      TextureUsage::RENDER_ATTACHMENT};
  auto texture = std::unique_ptr<EAGLLayerTexture>(new EAGLLayerTexture(descriptor, frameBufferID));
  gl->genRenderbuffers(1, &texture->renderBufferID);
  if (texture->renderBufferID == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to generate renderbuffer!");
    return nullptr;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, texture->renderBufferID);
  auto eaglContext = static_cast<EAGLGPU*>(gpu)->eaglContext();
  [eaglContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
  if (!CheckGLError(gl)) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to allocate renderbuffer storage!");
    return nullptr;
  }
  auto state = gpu->state();
  state->bindFramebuffer(texture.get());
  gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              texture->renderBufferID);
  auto frameBufferStatus = gl->checkFramebufferStatus(GL_FRAMEBUFFER);
  if (frameBufferStatus != GL_FRAMEBUFFER_COMPLETE) {
    LOGE("EAGLLayerTexture::MakeFrom() framebuffer is not complete!");
    return nullptr;
  }
  return texture;
}

void EAGLLayerTexture::onReleaseTexture(GLGPU* gpu) {
  auto gl = gpu->functions();
  if (_frameBufferID > 0) {
    auto state = gpu->state();
    state->bindFramebuffer(this);
    gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
    gl->deleteFramebuffers(1, &_frameBufferID);
    _frameBufferID = 0;
  }
  if (renderBufferID > 0) {
    gl->deleteRenderbuffers(1, &renderBufferID);
    renderBufferID = 0;
  }
}
}  // namespace tgfx
