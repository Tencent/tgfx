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
#include "core/utils/PixelFormatUtil.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLRenderPass.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
std::shared_ptr<RenderPass> GLCommandEncoder::onBeginRenderPass(
    const RenderPassDescriptor& descriptor) {
  if (descriptor.colorAttachments.empty()) {
    LOGE(
        "GLCommandEncoder::beginRenderPass() Invalid render pass descriptor, no color "
        "attachments!");
    return nullptr;
  }
  if (descriptor.colorAttachments.size() > 1) {
    LOGE(
        "GLCommandEncoder::onBeginRenderPass() Multiple color attachments are not yet supported in "
        "OpenGL!");
    return nullptr;
  }
  auto& colorAttachment = descriptor.colorAttachments[0];
  if (colorAttachment.texture == nullptr) {
    LOGE(
        "GLCommandEncoder::beginRenderPass() Invalid render pass descriptor, color attachment "
        "texture is null!");
    return nullptr;
  }
  if (colorAttachment.texture == colorAttachment.resolveTexture) {
    LOGE(
        "GLCommandEncoder::beginRenderPass() Invalid render pass descriptor, color attachment "
        "texture and resolve texture cannot be the same!");
    return nullptr;
  }
  auto& depthStencilAttachment = descriptor.depthStencilAttachment;
  if (depthStencilAttachment.texture &&
      depthStencilAttachment.texture->format() != PixelFormat::DEPTH24_STENCIL8) {
    LOGE(
        "GLCommandEncoder::beginRenderPass() Invalid render pass descriptor, depthStencil "
        "attachment texture format must be DEPTH24_STENCIL8!");
    return nullptr;
  }
  auto renderPass = std::make_shared<GLRenderPass>(gpu, descriptor);
  if (!renderPass->begin()) {
    return nullptr;
  }
  return renderPass;
}

void GLCommandEncoder::copyTextureToTexture(std::shared_ptr<GPUTexture> srcTexture,
                                            const Rect& srcRect,
                                            std::shared_ptr<GPUTexture> dstTexture,
                                            const Point& dstOffset) {
  if (srcTexture == nullptr || dstTexture == nullptr || srcRect.isEmpty()) {
    return;
  }
  if (!(srcTexture->usage() & GPUTextureUsage::RENDER_ATTACHMENT)) {
    LOGE("GLCommandEncoder::copyTextureToTexture() source texture is not copyable!");
    return;
  }
  auto gl = gpu->functions();
  auto state = gpu->state();
  auto glTexture = std::static_pointer_cast<GLTexture>(dstTexture);
  state->bindTexture(glTexture.get());
  state->bindFramebuffer(static_cast<GLTexture*>(srcTexture.get()));
  auto offsetX = static_cast<int>(dstOffset.x);
  auto offsetY = static_cast<int>(dstOffset.y);
  auto x = static_cast<int>(srcRect.left);
  auto y = static_cast<int>(srcRect.top);
  auto width = static_cast<int>(srcRect.width());
  auto height = static_cast<int>(srcRect.height());
  gl->copyTexSubImage2D(glTexture->target(), 0, offsetX, offsetY, x, y, width, height);
}

void GLCommandEncoder::copyTextureToBuffer(std::shared_ptr<GPUTexture> srcTexture,
                                           const Rect& srcRect,
                                           std::shared_ptr<GPUBuffer> dstBuffer, size_t dstOffset,
                                           size_t dstRowBytes) {
  if (srcTexture == nullptr || srcRect.isEmpty()) {
    LOGE("GLCommandEncoder::copyTextureToBuffer() source texture or rectangle is invalid!");
    return;
  }
  if (dstBuffer == nullptr || !(dstBuffer->usage() & GPUBufferUsage::READBACK)) {
    LOGE("GLCommandEncoder::copyTextureToBuffer() destination buffer is invalid!");
    return;
  }
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  if (!caps->isFormatRenderable(srcTexture->format())) {
    LOGE("GLCommandEncoder::copyTextureToBuffer() source texture format is not copyable!");
    return;
  }
  auto format = srcTexture->format();
  auto bytesPerPixel = PixelFormatBytesPerPixel(format);
  auto minRowBytes = static_cast<size_t>(srcRect.width()) * bytesPerPixel;
  if (dstRowBytes < minRowBytes) {
    LOGE("GLCommandEncoder::copyTextureToBuffer() dstRowBytes is too small!");
    return;
  }
  auto gl = gpu->functions();
  auto glTexture = static_cast<GLTexture*>(srcTexture.get());
  if (srcTexture->usage() & GPUTextureUsage::RENDER_ATTACHMENT) {
    auto state = gpu->state();
    state->bindFramebuffer(glTexture);
  } else if (!glTexture->checkFrameBuffer(gpu)) {
    LOGE(
        "GLCommandEncoder::copyTextureToBuffer() failed to create framebuffer for source texture!");
    return;
  }
  auto restoreGLRowLength = false;
  if (dstRowBytes != minRowBytes) {
    if (!caps->packRowLengthSupport) {
      LOGE("GLCommandEncoder::copyTextureToBuffer() custom dstRowBytes is not supported!");
      return;
    }
    gl->pixelStorei(GL_PACK_ROW_LENGTH, static_cast<int>(dstRowBytes / bytesPerPixel));
    restoreGLRowLength = true;
  }
  gl->pixelStorei(GL_PACK_ALIGNMENT, static_cast<int>(bytesPerPixel));
  auto glBuffer = static_cast<GLBuffer*>(dstBuffer.get());
  gl->bindBuffer(GL_PIXEL_PACK_BUFFER, glBuffer->bufferID());
  auto x = static_cast<int>(srcRect.left);
  auto y = static_cast<int>(srcRect.top);
  auto width = static_cast<int>(srcRect.width());
  auto height = static_cast<int>(srcRect.height());
  auto textureFormat = caps->getTextureFormat(format);
  gl->readPixels(x, y, width, height, textureFormat.externalFormat, textureFormat.externalType,
                 reinterpret_cast<void*>(dstOffset));
  if (restoreGLRowLength) {
    gl->pixelStorei(GL_PACK_ROW_LENGTH, 0);
  }
  glBuffer->insertReadbackFence();
}

void GLCommandEncoder::generateMipmapsForTexture(std::shared_ptr<GPUTexture> texture) {
  auto glTexture = static_cast<GLTexture*>(texture.get());
  if (glTexture->mipLevelCount() <= 1 || glTexture->target() != GL_TEXTURE_2D) {
    return;
  }
  auto state = gpu->state();
  state->bindTexture(glTexture);
  auto gl = gpu->functions();
  gl->generateMipmap(glTexture->target());
}

std::shared_ptr<CommandBuffer> GLCommandEncoder::onFinish() {
  // In OpenGL, we don't have a specific command buffer to return, so we just return an empty one.
  return std::make_shared<CommandBuffer>();
}

}  // namespace tgfx
