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

#include "gpu/opengl/GLTexture.h"
#include "GLCaps.h"
#include "GLGPU.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
GLTexture::GLTexture(const GPUTextureDescriptor& descriptor, unsigned target, unsigned textureID)
    : GPUTexture(descriptor), _target(target), _textureID(textureID) {
}

GPUTextureType GLTexture::type() const {
  switch (_target) {
    case GL_TEXTURE_2D:
      return GPUTextureType::TwoD;
    case GL_TEXTURE_RECTANGLE:
      return GPUTextureType::Rectangle;
    case GL_TEXTURE_EXTERNAL_OES:
      return GPUTextureType::External;
    default:
      return GPUTextureType::None;
  }
}

unsigned GLTexture::frameBufferID() const {
  // Ensure the framebuffer is created if needed.
  DEBUG_ASSERT(!(descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT) || textureFrameBuffer > 0 ||
               _textureID == 0);
  return textureFrameBuffer;
}

BackendTexture GLTexture::getBackendTexture() const {
  if (_textureID == 0 || !(descriptor.usage & GPUTextureUsage::TEXTURE_BINDING)) {
    return {};
  }
  GLTextureInfo textureInfo = {};
  textureInfo.id = _textureID;
  textureInfo.target = _target;
  textureInfo.format = PixelFormatToGLSizeFormat(format());
  return {textureInfo, width(), height()};
}

BackendRenderTarget GLTexture::getBackendRenderTarget() const {
  if (!(descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  GLFrameBufferInfo glInfo = {};
  glInfo.id = frameBufferID();
  glInfo.format = PixelFormatToGLSizeFormat(format());
  return {glInfo, width(), height()};
}

void GLTexture::release(GPU* gpu) {
  DEBUG_ASSERT(gpu != nullptr);
  auto glGPU = static_cast<GLGPU*>(gpu);
  if (textureFrameBuffer > 0) {
    auto gl = glGPU->functions();
    gl->bindFramebuffer(GL_FRAMEBUFFER, textureFrameBuffer);
    gl->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, _target, 0, 0);
    gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->deleteFramebuffers(1, &textureFrameBuffer);
    textureFrameBuffer = 0;
  }
  onRelease(glGPU);
}

void GLTexture::onRelease(GLGPU* gpu) {
  if (_textureID > 0) {
    auto gl = gpu->functions();
    gl->deleteTextures(1, &_textureID);
    _textureID = 0;
  }
}

bool GLTexture::checkFrameBuffer(GLGPU* gpu) {
  if (!(descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT) || textureFrameBuffer > 0 ||
      _textureID == 0) {
    return true;
  }
  DEBUG_ASSERT(gpu != nullptr);
  auto caps = gpu->caps();
  if (!caps->isFormatRenderable(format())) {
    LOGE("GLTexture::makeFrameBuffer() format is not renderable!");
    return false;
  }
  auto gl = gpu->functions();
  unsigned frameBufferID = 0;
  gl->genFramebuffers(1, &frameBufferID);
  if (frameBufferID == 0) {
    LOGE("GLTexture::makeFrameBuffer() failed to generate framebuffer!");
    return false;
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, frameBufferID);
  gl->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, _target, _textureID, 0);
#ifndef TGFX_BUILD_FOR_WEB
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    gl->deleteFramebuffers(1, &frameBufferID);
    LOGE("GLTexture::makeFrameBuffer() framebuffer is not complete!");
    return false;
  }
#endif
  textureFrameBuffer = frameBufferID;
  return true;
}

}  // namespace tgfx