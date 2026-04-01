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
static unsigned CreateFramebufferForRenderbuffer(GLGPU* gpu, unsigned renderbufferID) {
  auto gl = gpu->functions();
  unsigned fbo = 0;
  gl->genFramebuffers(1, &fbo);
  if (fbo == 0) {
    return 0;
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              renderbufferID);
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    gl->deleteFramebuffers(1, &fbo);
    return 0;
  }
  return fbo;
}

static unsigned CreateLayerRenderbuffer(GLGPU* gpu, CAEAGLLayer* layer) {
  auto gl = gpu->functions();
  ClearGLError(gl);
  unsigned renderbufferID = 0;
  gl->genRenderbuffers(1, &renderbufferID);
  if (renderbufferID == 0) {
    return 0;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, renderbufferID);
  auto eaglContext = static_cast<EAGLGPU*>(gpu)->eaglContext();
  [eaglContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
  if (!CheckGLError(gl)) {
    gl->deleteRenderbuffers(1, &renderbufferID);
    return 0;
  }
  return renderbufferID;
}

static unsigned CreateMSAARenderbuffer(GLGPU* gpu, int sampleCount, int width, int height) {
  auto gl = gpu->functions();
  ClearGLError(gl);
  unsigned renderbufferID = 0;
  gl->genRenderbuffers(1, &renderbufferID);
  if (renderbufferID == 0) {
    return 0;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, renderbufferID);
  auto format = gpu->caps()->getTextureFormat(PixelFormat::RGBA_8888).sizedFormat;
  gl->renderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, format, width, height);
  if (!CheckGLError(gl)) {
    gl->deleteRenderbuffers(1, &renderbufferID);
    return 0;
  }
  return renderbufferID;
}

std::shared_ptr<EAGLLayerTexture> EAGLLayerTexture::MakeFrom(GLGPU* gpu, CAEAGLLayer* layer,
                                                             int sampleCount) {
  DEBUG_ASSERT(gpu != nullptr);
  if (layer == nil) {
    return nullptr;
  }
  auto width = static_cast<int>(layer.bounds.size.width * layer.contentsScale);
  auto height = static_cast<int>(layer.bounds.size.height * layer.contentsScale);
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  sampleCount = gpu->getSampleCount(sampleCount, PixelFormat::RGBA_8888);
  auto resolveRBID = CreateLayerRenderbuffer(gpu, layer);
  if (resolveRBID == 0) {
    LOGE("EAGLLayerTexture::MakeFrom() failed to create layer renderbuffer!");
    return nullptr;
  }
  auto gl = gpu->functions();
  unsigned frameBufferID = 0, resolveFBO = 0, msaaRBID = 0;
  bool success = false;
  do {
    if (sampleCount <= 1) {
      frameBufferID = CreateFramebufferForRenderbuffer(gpu, resolveRBID);
      if (frameBufferID == 0) {
        LOGE("EAGLLayerTexture::MakeFrom() failed to create framebuffer!");
        break;
      }
    } else {
      resolveFBO = CreateFramebufferForRenderbuffer(gpu, resolveRBID);
      if (resolveFBO == 0) {
        LOGE("EAGLLayerTexture::MakeFrom() failed to create resolve framebuffer!");
        break;
      }
      msaaRBID = CreateMSAARenderbuffer(gpu, sampleCount, width, height);
      if (msaaRBID == 0) {
        LOGE("EAGLLayerTexture::MakeFrom() failed to create MSAA renderbuffer!");
        break;
      }
      frameBufferID = CreateFramebufferForRenderbuffer(gpu, msaaRBID);
      if (frameBufferID == 0) {
        LOGE("EAGLLayerTexture::MakeFrom() failed to create MSAA framebuffer!");
        break;
      }
    }
    success = true;
  } while (false);
  if (!success) {
    if (msaaRBID > 0) {
      gl->deleteRenderbuffers(1, &msaaRBID);
    }
    if (resolveFBO > 0) {
      gl->deleteFramebuffers(1, &resolveFBO);
    }
    gl->deleteRenderbuffers(1, &resolveRBID);
    return nullptr;
  }
  TextureDescriptor descriptor = {width, height,      PixelFormat::RGBA_8888,
                                  false, sampleCount, TextureUsage::RENDER_ATTACHMENT};
  auto texture = gpu->makeResource<EAGLLayerTexture>(descriptor, frameBufferID);
  texture->msaaBufferID = msaaRBID;
  texture->_colorBufferID = resolveRBID;
  texture->_resolveFrameBufferID = resolveFBO;
  gpu->state()->bindFramebuffer(texture.get());
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
