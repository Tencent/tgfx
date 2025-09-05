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
#include "GLUtil.h"
#include "gpu/DrawingManager.h"
#include "gpu/GlobalCache.h"
#include "gpu/opengl/GLRenderPipeline.h"
#include "gpu/opengl/GLTexture.h"

namespace tgfx {
GLRenderPass::GLRenderPass(std::shared_ptr<GLInterface> interface, RenderPassDescriptor descriptor)
    : RenderPass(std::move(descriptor)), interface(std::move(interface)) {
}

void GLRenderPass::begin() {
  DEBUG_ASSERT(!descriptor.colorAttachments.empty());
  auto& colorAttachment = descriptor.colorAttachments[0];
  DEBUG_ASSERT(colorAttachment.texture != nullptr);
  auto renderTexture = static_cast<GLTexture*>(colorAttachment.texture);
  auto gl = interface->functions();
  ClearGLError(gl);
  gl->bindFramebuffer(GL_FRAMEBUFFER, renderTexture->frameBufferID());
  // Set the viewport to cover the entire color attachment by default.
  gl->viewport(0, 0, renderTexture->width(), renderTexture->height());
  // Disable scissor test by default.
  gl->disable(GL_SCISSOR_TEST);
  if (colorAttachment.loadAction == LoadAction::Clear) {
    auto& color = colorAttachment.clearValue;
    gl->clearColor(color.red, color.green, color.blue, color.alpha);
    gl->clear(GL_COLOR_BUFFER_BIT);
  }
}

void GLRenderPass::setScissorRect(int x, int y, int width, int height) {
  auto gl = interface->functions();
  auto texture = descriptor.colorAttachments[0].texture;
  if (x == 0 && y == 0 && width == texture->width() && height == texture->height()) {
    gl->disable(GL_SCISSOR_TEST);
  } else {
    gl->enable(GL_SCISSOR_TEST);
    gl->scissor(x, y, width, height);
  }
}

void GLRenderPass::setPipeline(GPURenderPipeline* pipeline) {
  DEBUG_ASSERT(pipeline != nullptr);
  if (renderPipeline == pipeline) {
    return;
  }
  renderPipeline = static_cast<GLRenderPipeline*>(pipeline);
  renderPipeline->activate(interface.get());
}

void GLRenderPass::setUniformBytes(unsigned, const void* data, size_t size) {
  DEBUG_ASSERT(renderPipeline != nullptr);
  renderPipeline->setUniformBytes(interface.get(), data, size);
}

void GLRenderPass::setTexture(unsigned binding, GPUTexture* texture, GPUSampler* sampler) {
  DEBUG_ASSERT(texture != nullptr);
  auto glSampler = static_cast<GLSampler*>(sampler);
  static_cast<GLTexture*>(texture)->bindTexture(interface.get(), binding, glSampler);
  auto renderTexture = descriptor.colorAttachments[0].texture;
  if (texture == renderTexture && interface->caps()->textureRedSupport) {
    auto gl = interface->functions();
    gl->textureBarrier();
  }
}

void GLRenderPass::setVertexBuffer(GPUBuffer* buffer, size_t offset) {
  DEBUG_ASSERT(renderPipeline != nullptr);
  renderPipeline->setVertexBuffer(interface.get(), buffer, offset);
}

void GLRenderPass::setIndexBuffer(GPUBuffer* buffer, IndexFormat format) {
  DEBUG_ASSERT(renderPipeline != nullptr);
  auto bufferID = buffer ? static_cast<GLBuffer*>(buffer)->bufferID() : 0;
  auto gl = interface->functions();
  gl->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferID);
  indexFormat = format;
}

void GLRenderPass::onEnd() {
  auto gl = interface->functions();
  auto caps = interface->caps();
  auto& attachment = descriptor.colorAttachments[0];
  if (attachment.resolveTexture) {
    auto renderTexture = static_cast<GLTexture*>(attachment.texture);
    auto sampleTexture = static_cast<GLTexture*>(attachment.resolveTexture);
    DEBUG_ASSERT(renderTexture != sampleTexture);
    gl->bindFramebuffer(GL_READ_FRAMEBUFFER, renderTexture->frameBufferID());
    gl->bindFramebuffer(GL_DRAW_FRAMEBUFFER, sampleTexture->frameBufferID());
    // MSAA resolve may be affected by the scissor test, so disable it here.
    gl->disable(GL_SCISSOR_TEST);
    if (caps->msFBOType == MSFBOType::ES_Apple) {
      gl->resolveMultisampleFramebuffer();
    } else {
      gl->blitFramebuffer(0, 0, renderTexture->width(), renderTexture->height(), 0, 0,
                          sampleTexture->width(), sampleTexture->height(), GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    }
  }
  if (caps->vertexArrayObjectSupport) {
    gl->bindVertexArray(0);
  }
  gl->bindFramebuffer(GL_FRAMEBUFFER, 0);
}

static constexpr unsigned PrimitiveTypes[] = {GL_TRIANGLES, GL_TRIANGLE_STRIP};

void GLRenderPass::draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) {
  auto gl = interface->functions();
  gl->drawArrays(PrimitiveTypes[static_cast<int>(primitiveType)], static_cast<int>(baseVertex),
                 static_cast<int>(vertexCount));
}

void GLRenderPass::drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) {
  auto gl = interface->functions();
  unsigned indexType = (indexFormat == IndexFormat::UInt16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
  size_t indexSize = (indexFormat == IndexFormat::UInt16) ? sizeof(uint16_t) : sizeof(uint32_t);
  gl->drawElements(PrimitiveTypes[static_cast<int>(primitiveType)], static_cast<int>(indexCount),
                   indexType, reinterpret_cast<void*>(baseIndex * indexSize));
}
}  // namespace tgfx
