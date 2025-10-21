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

#include "GLRenderPass.h"
#include "GLSampler.h"
#include "gpu/DrawingManager.h"
#include "gpu/opengl/GLDepthStencilTexture.h"
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLRenderPipeline.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
GLRenderPass::GLRenderPass(GLGPU* gpu, RenderPassDescriptor descriptor)
    : RenderPass(std::move(descriptor)), gpu(gpu) {
}

bool GLRenderPass::begin() {
  DEBUG_ASSERT(!descriptor.colorAttachments.empty());
  auto& colorAttachment = descriptor.colorAttachments[0];
  DEBUG_ASSERT(colorAttachment.texture != nullptr);
  auto renderTexture = static_cast<GLTexture*>(colorAttachment.texture.get());
  auto state = gpu->state();
  state->bindFramebuffer(renderTexture);
  auto gl = gpu->functions();

  auto& depthStencilAttachment = descriptor.depthStencilAttachment;
  if (depthStencilAttachment.texture != nullptr) {
    auto depthStencilTexture =
        static_cast<GLDepthStencilTexture*>(depthStencilAttachment.texture.get());
    gl->bindRenderbuffer(GL_RENDERBUFFER, depthStencilTexture->renderBufferID());
    gl->framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                depthStencilTexture->renderBufferID());
#ifndef TGFX_BUILD_FOR_WEB
    if (gl->checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      LOGE("GLCommandEncoder::beginRenderPass() depthStencil attachment can not be attached!");
      return false;
    }
#endif
    if (depthStencilAttachment.loadAction == LoadAction::Clear) {
      gl->clearDepthf(depthStencilAttachment.depthClearValue);
      gl->clearStencil(static_cast<int>(depthStencilAttachment.stencilClearValue));
      gl->clear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
  }
  // Set the viewport to cover the entire color attachment by default.
  state->setViewport(0, 0, renderTexture->width(), renderTexture->height());
  // Disable scissor test by default.
  state->setEnabled(GL_SCISSOR_TEST, false);
  if (colorAttachment.resolveTexture && gpu->caps()->multisampleDisableSupport) {
    state->setEnabled(GL_MULTISAMPLE, true);
  }
  if (colorAttachment.loadAction == LoadAction::Clear) {
    state->setClearColor(colorAttachment.clearValue);
    gl->clear(GL_COLOR_BUFFER_BIT);
  }
  return true;
}

void GLRenderPass::setViewport(int x, int y, int width, int height) {
  auto state = gpu->state();
  state->setViewport(x, y, width, height);
}

void GLRenderPass::setScissorRect(int x, int y, int width, int height) {
  auto texture = descriptor.colorAttachments[0].texture;
  auto state = gpu->state();
  if (x == 0 && y == 0 && width == texture->width() && height == texture->height()) {
    state->setEnabled(GL_SCISSOR_TEST, false);
  } else {
    state->setEnabled(GL_SCISSOR_TEST, true);
    state->setScissorRect(x, y, width, height);
  }
}

void GLRenderPass::setPipeline(std::shared_ptr<RenderPipeline> pipeline) {
  if (renderPipeline == pipeline) {
    return;
  }
  renderPipeline = std::static_pointer_cast<GLRenderPipeline>(pipeline);
  if (renderPipeline != nullptr) {
    auto& attachment = descriptor.depthStencilAttachment;
    renderPipeline->activate(gpu, attachment.depthReadOnly, attachment.stencilReadOnly,
                             stencilReference);
  }
}

void GLRenderPass::setUniformBuffer(unsigned binding, std::shared_ptr<GPUBuffer> buffer,
                                    size_t offset, size_t size) {
  if (renderPipeline == nullptr) {
    LOGE("GLRenderPass::setUniformBuffer: renderPipeline is nullptr!");
    return;
  }

  renderPipeline->setUniformBuffer(gpu, binding, static_cast<GLBuffer*>(buffer.get()), offset,
                                   size);
}

void GLRenderPass::setTexture(unsigned binding, std::shared_ptr<GPUTexture> texture,
                              std::shared_ptr<GPUSampler> sampler) {
  if (texture == nullptr) {
    return;
  }
  if (renderPipeline == nullptr) {
    LOGE("GLRenderPass::setTexture: renderPipeline is null!");
    return;
  }
  renderPipeline->setTexture(gpu, binding, static_cast<GLTexture*>(texture.get()),
                             static_cast<GLSampler*>(sampler.get()));
  auto renderTexture = descriptor.colorAttachments[0].texture;
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  if (texture == renderTexture && caps->textureBarrierSupport) {
    auto gl = gpu->functions();
    gl->textureBarrier();
  }
}

void GLRenderPass::setVertexBuffer(std::shared_ptr<GPUBuffer> buffer, size_t offset) {
  if (renderPipeline == nullptr) {
    LOGE("GLRenderPass::setVertexBuffer: renderPipeline is null!");
    return;
  }
  renderPipeline->setVertexBuffer(gpu, static_cast<GLBuffer*>(buffer.get()), offset);
}

void GLRenderPass::setIndexBuffer(std::shared_ptr<GPUBuffer> buffer, IndexFormat format) {
  if (renderPipeline == nullptr) {
    LOGE("GLRenderPass::setIndexBuffer: renderPipeline is null!");
    return;
  }
  auto bufferID = buffer ? static_cast<GLBuffer*>(buffer.get())->bufferID() : 0;
  auto gl = gpu->functions();
  gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferID);
  indexFormat = format;
}

void GLRenderPass::setStencilReference(uint32_t reference) {
  if (reference == stencilReference) {
    return;
  }
  if (renderPipeline != nullptr) {
    renderPipeline->setStencilReference(gpu, reference);
  }
  stencilReference = reference;
}

void GLRenderPass::onEnd() {
  auto gl = gpu->functions();
  auto& attachment = descriptor.colorAttachments[0];
  if (attachment.resolveTexture) {
    auto renderTexture = static_cast<GLTexture*>(attachment.texture.get());
    auto sampleTexture = static_cast<GLTexture*>(attachment.resolveTexture.get());
    DEBUG_ASSERT(renderTexture != sampleTexture);
    auto state = gpu->state();
    state->bindFramebuffer(renderTexture, FrameBufferTarget::Read);
    state->bindFramebuffer(sampleTexture, FrameBufferTarget::Draw);
    // MSAA resolve may be affected by the scissor test, so disable it here.
    state->setEnabled(GL_SCISSOR_TEST, false);
    gl->blitFramebuffer(0, 0, renderTexture->width(), renderTexture->height(), 0, 0,
                        sampleTexture->width(), sampleTexture->height(), GL_COLOR_BUFFER_BIT,
                        GL_NEAREST);
  }
}

static constexpr unsigned PrimitiveTypes[] = {GL_TRIANGLES, GL_TRIANGLE_STRIP};

void GLRenderPass::draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) {
  auto gl = gpu->functions();
  gl->drawArrays(PrimitiveTypes[static_cast<int>(primitiveType)], static_cast<int>(baseVertex),
                 static_cast<int>(vertexCount));
}

void GLRenderPass::drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) {
  auto gl = gpu->functions();
  unsigned indexType = (indexFormat == IndexFormat::UInt16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
  size_t indexSize = (indexFormat == IndexFormat::UInt16) ? sizeof(uint16_t) : sizeof(uint32_t);
  gl->drawElements(PrimitiveTypes[static_cast<int>(primitiveType)], static_cast<int>(indexCount),
                   indexType, reinterpret_cast<void*>(baseIndex * indexSize));
}
}  // namespace tgfx
