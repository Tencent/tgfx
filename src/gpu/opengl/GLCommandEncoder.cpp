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

#include "GLCommandEncoder.h"
#include "gpu/opengl/GLFrameBuffer.h"
#include "gpu/opengl/GLRenderPass.h"
#include "gpu/opengl/GLSemaphore.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
std::shared_ptr<RenderPass> GLCommandEncoder::onBeginRenderPass(
    std::shared_ptr<RenderTarget> renderTarget, bool resolveMSAA) {
  if (renderTarget == nullptr) {
    return nullptr;
  }
  auto renderPass = std::make_shared<GLRenderPass>(interface, std::move(renderTarget), resolveMSAA);
  renderPass->begin();
  return renderPass;
}

void GLCommandEncoder::copyFrameBufferToTexture(GPUFrameBuffer* frameBuffer, const Point& srcOffset,
                                                GPUTexture* texture, const Rect& dstRect) {
  if (frameBuffer == nullptr || texture == nullptr || dstRect.isEmpty()) {
    return;
  }
  auto gl = interface->functions();
  auto glRenderTarget = static_cast<GLFrameBuffer*>(frameBuffer);
  gl->bindFramebuffer(GL_FRAMEBUFFER, glRenderTarget->readFrameBufferID());
  auto glTexture = static_cast<const GLTexture*>(texture);
  auto target = glTexture->target();
  gl->bindTexture(target, glTexture->id());
  auto offsetX = static_cast<int>(dstRect.left);
  auto offsetY = static_cast<int>(dstRect.top);
  auto x = static_cast<int>(srcOffset.x);
  auto y = static_cast<int>(srcOffset.y);
  auto width = static_cast<int>(dstRect.width());
  auto height = static_cast<int>(dstRect.height());
  gl->copyTexSubImage2D(target, 0, offsetX, offsetY, x, y, width, height);
}

void GLCommandEncoder::generateMipmapsForTexture(GPUTexture* texture) {
  auto glTexture = static_cast<GLTexture*>(texture);
  if (glTexture->mipLevelCount() <= 1 || glTexture->target() != GL_TEXTURE_2D) {
    return;
  }
  auto gl = interface->functions();
  gl->bindTexture(glTexture->target(), glTexture->id());
  gl->generateMipmap(glTexture->target());
}

BackendSemaphore GLCommandEncoder::insertSemaphore() {
  if (!interface->caps()->semaphoreSupport) {
    return {};
  }
  auto gl = interface->functions();
  GLSyncInfo glSync = {};
  glSync.sync = gl->fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (glSync.sync) {
    // If we inserted semaphores during the flush, we need to call glFlush.
    gl->flush();
    return {glSync};
  }
  return {};
}

void GLCommandEncoder::waitSemaphore(const BackendSemaphore& semaphore) {
  GLSyncInfo glSync = {};
  if (!semaphore.getGLSync(&glSync)) {
    return;
  }
  auto gl = interface->functions();
  gl->waitSync(glSync.sync, 0, GL_TIMEOUT_IGNORED);
}

std::shared_ptr<CommandBuffer> GLCommandEncoder::onFinish() {
  // In OpenGL, we don't have a specific command buffer to return, so we just return an empty one.
  return std::make_shared<CommandBuffer>();
}

}  // namespace tgfx
