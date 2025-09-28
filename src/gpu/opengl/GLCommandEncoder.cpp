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
#include "gpu/opengl/GLFence.h"
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
