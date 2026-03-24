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
std::shared_ptr<EAGLLayerTexture> EAGLLayerTexture::MakeFrom(GLGPU* gpu, CAEAGLLayer* layer,
                                                             int sampleCount) {
  DEBUG_ASSERT(gpu != nullptr);
  if (layer == nil) {
    return nullptr;
  }
  auto width = layer.bounds.size.width * layer.contentsScale;
  auto height = layer.bounds.size.height * layer.contentsScale;
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  sampleCount = gpu->getSampleCount(sampleCount, PixelFormat::RGBA_8888);
  auto gl = gpu->functions();
  ClearGLError(gl);
  // Create the resolve renderbuffer backed by the CAEAGLLayer.
  unsigned resolveRBID = 0;
  gl->genRenderbuffers(1, &resolveRBID);
  if (resolveRBID == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to generate resolve renderbuffer!");
    return nullptr;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, resolveRBID);
  auto eaglContext = static_cast<EAGLGPU*>(gpu)->eaglContext();
  [eaglContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
  if (!CheckGLError(gl)) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to allocate renderbuffer storage!");
    return nullptr;
  }
  if (sampleCount <= 1) {
    // No MSAA: single framebuffer with the layer renderbuffer attached.
    unsigned frameBufferID = 0;
    gl->genFramebuffers(1, &frameBufferID);
    if (frameBufferID == 0) {
      LOGE("EAGLLayerTexture::MakeFrom() failed to generate framebuffer!");
      gl->deleteRenderbuffers(1, &resolveRBID);
      return nullptr;
    }
    TextureDescriptor descriptor = {
        static_cast<int>(width),        static_cast<int>(height), PixelFormat::RGBA_8888, false, 1,
        TextureUsage::RENDER_ATTACHMENT};
    auto texture = gpu->makeResource<EAGLLayerTexture>(descriptor, frameBufferID);
    texture->_colorBufferID = resolveRBID;
    auto state = gpu->state();
    state->bindFramebuffer(texture.get());
    gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolveRBID);
    if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      LOGE("EAGLLayerTexture::MakeFrom() framebuffer is not complete!");
      return nullptr;
    }
    return texture;
  }
  // MSAA path: create a MSAA renderbuffer for rendering, and a separate resolve framebuffer for
  // the layer renderbuffer.
  unsigned msaaFBO = 0;
  gl->genFramebuffers(1, &msaaFBO);
  if (msaaFBO == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to generate MSAA framebuffer!");
    gl->deleteRenderbuffers(1, &resolveRBID);
    return nullptr;
  }
  unsigned msaaRBID = 0;
  gl->genRenderbuffers(1, &msaaRBID);
  if (msaaRBID == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to generate MSAA renderbuffer!");
    gl->deleteFramebuffers(1, &msaaFBO);
    gl->deleteRenderbuffers(1, &resolveRBID);
    return nullptr;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, msaaRBID);
  auto format = gpu->caps()->getTextureFormat(PixelFormat::RGBA_8888).sizedFormat;
  gl->renderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, format, static_cast<int>(width),
                                     static_cast<int>(height));
  if (!CheckGLError(gl)) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to allocate MSAA renderbuffer storage!");
    gl->deleteRenderbuffers(1, &msaaRBID);
    gl->deleteFramebuffers(1, &msaaFBO);
    gl->deleteRenderbuffers(1, &resolveRBID);
    return nullptr;
  }
  TextureDescriptor descriptor = {static_cast<int>(width),
                                  static_cast<int>(height),
                                  PixelFormat::RGBA_8888,
                                  false,
                                  sampleCount,
                                  TextureUsage::RENDER_ATTACHMENT};
  auto texture = gpu->makeResource<EAGLLayerTexture>(descriptor, msaaFBO);
  texture->msaaBufferID = msaaRBID;
  texture->_colorBufferID = resolveRBID;
  auto state = gpu->state();
  state->bindFramebuffer(texture.get());
  gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaRBID);
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    LOGE("EAGLLayerTexture::MakeFrom() MSAA framebuffer is not complete!");
    return nullptr;
  }
  // Create the resolve framebuffer with the layer renderbuffer attached.
  unsigned resolveFBO = 0;
  gl->genFramebuffers(1, &resolveFBO);
  if (resolveFBO == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to generate resolve framebuffer!");
    return nullptr;
  }
  texture->_resolveFrameBufferID = resolveFBO;
  gl->bindFramebuffer(GL_FRAMEBUFFER, resolveFBO);
  gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolveRBID);
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    LOGE("EAGLLayerTexture::MakeFrom() resolve framebuffer is not complete!");
    return nullptr;
  }
  return texture;
}

void EAGLLayerTexture::onReleaseTexture(GLGPU* gpu) {
  auto gl = gpu->functions();
  if (_resolveFrameBufferID > 0) {
    gl->bindFramebuffer(GL_FRAMEBUFFER, _resolveFrameBufferID);
    gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
    gl->deleteFramebuffers(1, &_resolveFrameBufferID);
    _resolveFrameBufferID = 0;
  }
  if (_frameBufferID > 0) {
    auto state = gpu->state();
    state->bindFramebuffer(this);
    gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);
    gl->deleteFramebuffers(1, &_frameBufferID);
    _frameBufferID = 0;
  }
  if (msaaBufferID > 0) {
    gl->deleteRenderbuffers(1, &msaaBufferID);
    msaaBufferID = 0;
  }
  if (_colorBufferID > 0) {
    gl->deleteRenderbuffers(1, &_colorBufferID);
    _colorBufferID = 0;
  }
}
}  // namespace tgfx
