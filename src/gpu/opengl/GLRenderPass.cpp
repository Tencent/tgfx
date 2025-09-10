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
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLRenderPipeline.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
GLRenderPass::GLRenderPass(GLGPU* gpu, RenderPassDescriptor descriptor)
    : RenderPass(std::move(descriptor)), gpu(gpu) {
}

void GLRenderPass::begin() {
  DEBUG_ASSERT(!descriptor.colorAttachments.empty());
  auto& colorAttachment = descriptor.colorAttachments[0];
  DEBUG_ASSERT(colorAttachment.texture != nullptr);
  auto renderTexture = static_cast<GLTexture*>(colorAttachment.texture);
  gpu->bindFramebuffer(renderTexture);
  auto gl = gpu->functions();
  // Set the viewport to cover the entire color attachment by default.
  gpu->setViewport(0, 0, renderTexture->width(), renderTexture->height());
  // Disable scissor test by default.
  gpu->enableCapability(GL_SCISSOR_TEST, false);
  if (colorAttachment.loadAction == LoadAction::Clear) {
    gpu->setClearColor(colorAttachment.clearValue);
    gl->clear(GL_COLOR_BUFFER_BIT);
  }
}

void GLRenderPass::setScissorRect(int x, int y, int width, int height) {
  auto texture = descriptor.colorAttachments[0].texture;
  if (x == 0 && y == 0 && width == texture->width() && height == texture->height()) {
    gpu->enableCapability(GL_SCISSOR_TEST, false);
  } else {
    gpu->enableCapability(GL_SCISSOR_TEST, true);
    gpu->setScissorRect(x, y, width, height);
  }
}

void GLRenderPass::setPipeline(GPURenderPipeline* pipeline) {
  DEBUG_ASSERT(pipeline != nullptr);
  if (renderPipeline == pipeline) {
    return;
  }
  renderPipeline = static_cast<GLRenderPipeline*>(pipeline);
  renderPipeline->activate(gpu);
}

void GLRenderPass::setUniformBytes(unsigned binding, const void* data, size_t size) {
  DEBUG_ASSERT(renderPipeline != nullptr);
  renderPipeline->setUniformBytes(gpu, binding, data, size);
}

void GLRenderPass::setTexture(unsigned binding, GPUTexture* texture, GPUSampler* sampler) {
  DEBUG_ASSERT(texture != nullptr);
  auto glTexture = static_cast<GLTexture*>(texture);
  auto glSampler = static_cast<GLSampler*>(sampler);
  gpu->bindTexture(glTexture, binding);
  glTexture->updateSampler(gpu, glSampler);
  auto renderTexture = descriptor.colorAttachments[0].texture;
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  if (texture == renderTexture && caps->textureRedSupport) {
    auto gl = gpu->functions();
    gl->textureBarrier();
  }
}

void GLRenderPass::setVertexBuffer(GPUBuffer* buffer, size_t offset) {
  DEBUG_ASSERT(renderPipeline != nullptr);
  renderPipeline->setVertexBuffer(gpu, buffer, offset);
}

void GLRenderPass::setIndexBuffer(GPUBuffer* buffer, IndexFormat format) {
  DEBUG_ASSERT(renderPipeline != nullptr);
  auto bufferID = buffer ? static_cast<GLBuffer*>(buffer)->bufferID() : 0;
  auto gl = gpu->functions();
  gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferID);
  indexFormat = format;
}

void GLRenderPass::onEnd() {
  auto gl = gpu->functions();
  auto caps = static_cast<const GLCaps*>(gpu->caps());
  auto& attachment = descriptor.colorAttachments[0];
  if (attachment.resolveTexture) {
    auto renderTexture = static_cast<GLTexture*>(attachment.texture);
    auto sampleTexture = static_cast<GLTexture*>(attachment.resolveTexture);
    DEBUG_ASSERT(renderTexture != sampleTexture);
    gpu->bindFramebuffer(renderTexture, FrameBufferTarget::Read);
    gpu->bindFramebuffer(sampleTexture, FrameBufferTarget::Draw);
    // MSAA resolve may be affected by the scissor test, so disable it here.
    gpu->enableCapability(GL_SCISSOR_TEST, false);
    if (caps->msFBOType == MSFBOType::ES_Apple) {
      gl->resolveMultisampleFramebuffer();
    } else {
      gl->blitFramebuffer(0, 0, renderTexture->width(), renderTexture->height(), 0, 0,
                          sampleTexture->width(), sampleTexture->height(), GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }
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
