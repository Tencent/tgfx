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

#include "GLMultisampleTexture.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLUtil.h"

namespace tgfx {
static bool RenderbufferStorageMSAA(GLGPU* gpu, int sampleCount, PixelFormat pixelFormat, int width,
                                    int height) {
  auto gl = gpu->functions();
  ClearGLError(gl);
  auto format = gpu->caps()->getTextureFormat(pixelFormat).sizedFormat;
  gl->renderbufferStorageMultisample(GL_RENDERBUFFER, sampleCount, format, width, height);
  return CheckGLError(gl);
}

std::shared_ptr<GLMultisampleTexture> GLMultisampleTexture::MakeFrom(
    GLGPU* gpu, const GPUTextureDescriptor& descriptor) {
  DEBUG_ASSERT(gpu != nullptr);
  DEBUG_ASSERT(descriptor.sampleCount > 1);
  if (!(descriptor.usage & GPUTextureUsage::RENDER_ATTACHMENT)) {
    LOGE("GLMultisampleTexture::MakeFrom() usage does not include RENDER_ATTACHMENT!");
    return nullptr;
  }
  if (descriptor.usage & GPUTextureUsage::TEXTURE_BINDING) {
    LOGE("GLMultisampleTexture::MakeFrom() usage includes TEXTURE_BINDING!");
    return nullptr;
  }
  if (descriptor.mipLevelCount > 1) {
    LOGE("GLMultisampleTexture::MakeFrom() mipLevelCount should be 1 for multisample textures!");
    return nullptr;
  }
  if (!gpu->isFormatRenderable(descriptor.format)) {
    LOGE("GLMultisampleTexture::MakeFrom() format is not renderable!");
    return nullptr;
  }
  unsigned frameBufferID = 0;
  auto gl = gpu->functions();
  gl->genFramebuffers(1, &frameBufferID);
  if (frameBufferID == 0) {
    LOGE("GLMultisampleTexture::MakeFrom() failed to generate framebuffer!");
    return nullptr;
  }
  auto texture = gpu->makeResource<GLMultisampleTexture>(descriptor, frameBufferID);
  gl->genRenderbuffers(1, &texture->renderBufferID);
  if (texture->renderBufferID == 0) {
    LOGE("GLMultisampleTexture::MakeFrom() failed to generate renderbuffer!");
    return nullptr;
  }
  gl->bindRenderbuffer(GL_RENDERBUFFER, texture->renderBufferID);
  if (!RenderbufferStorageMSAA(gpu, descriptor.sampleCount, descriptor.format, descriptor.width,
                               descriptor.height)) {
    return nullptr;
  }
  auto state = gpu->state();
  state->bindFramebuffer(texture.get());
  gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                              texture->renderBufferID);
#ifndef TGFX_BUILD_FOR_WEB
  if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    LOGE("GLMultisampleTexture::MakeFrom() framebuffer is not complete!");
    return nullptr;
  }
#endif
  return texture;
}

void GLMultisampleTexture::onReleaseTexture(GLGPU* gpu) {
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
