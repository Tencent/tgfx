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

#pragma once

#include "core/utils/Log.h"
#include "gpu/GPUBuffer.h"
#include "gpu/GPURenderPipeline.h"
#include "gpu/GPUSampler.h"
#include "gpu/RenderPassDescriptor.h"

namespace tgfx {
/**
 * Geometric primitives used for drawing.
 */
enum class PrimitiveType {
  Triangles,
  TriangleStrip,
};

/**
 * Index formats for indexed drawing.
 */
enum class IndexFormat {
  UInt16,
  UInt32,
};

/**
 * RenderPass represents an interface for encoding a sequence of rendering commands into a command
 * buffer. A RenderPass is begun by calling CommandEncoder::beginRenderPass() with a valid
 * RenderPassDescriptor, and must be ended by calling end() before beginning a new render pass.
 */
class RenderPass {
 public:
  virtual ~RenderPass() = default;

  /**
   * Sets the scissor rectangle used during the rasterization stage. After transformation into
   * viewport coordinates any fragments that fall outside the scissor rectangle will be discarded.
   */
  virtual void setScissorRect(int x, int y, int width, int height) = 0;

  /**
   * Sets the render pipeline to be used for subsequent draw calls. The pipeline defines the shader
   * programs and fixed-function state used during rendering.
   */
  virtual void setPipeline(GPURenderPipeline* pipeline) = 0;

  /**
   * Sets the uniform data to a specified binding index in the shader's UBO table.
   */
  virtual void setUniformBytes(unsigned binding, const void* data, size_t size) = 0;

  /**
   * Sets a texture and its sampler state to a specified binding index in the shader's texture table.
   */
  virtual void setTexture(unsigned binding, GPUTexture* texture, GPUSampler* sampler) = 0;

  /**
   * Sets or unsets the current vertex buffer with an optional offset.
   */
  virtual void setVertexBuffer(GPUBuffer* buffer, size_t offset = 0) = 0;

  /**
   * Sets the current index buffer with its format.
   */
  virtual void setIndexBuffer(GPUBuffer* buffer, IndexFormat format = IndexFormat::UInt16) = 0;

  /**
   * Draws primitives based on the vertex buffer provided by setVertexBuffer().
   */
  virtual void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) = 0;

  /**
   * Draws indexed primitives based on the index buffer provided by setIndexBuffer() and the vertex
   * buffer provided by setVertexBuffer().
   */
  virtual void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) = 0;

  /**
   * Completes the current render pass. After calling this method, no further commands can be added
   * to the render pass, and a new render pass can be started by calling
   * CommandEncoder::beginRenderPass() again.
   */
  void end() {
    onEnd();
    isEnd = true;
  }

 protected:
  RenderPassDescriptor descriptor = {};

  explicit RenderPass(RenderPassDescriptor descriptor) : descriptor(std::move(descriptor)) {
  }

  virtual void onEnd() = 0;

 private:
  bool isEnd = false;

  friend class CommandEncoder;
};
}  // namespace tgfx
