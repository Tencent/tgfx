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

#include <utility>
#include "gpu/GPUBuffer.h"
#include "gpu/ProgramInfo.h"
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
   * Completes the current render pass. After calling this method, no further commands can be added
   * to the render pass, and a new render pass can be started by calling
   * CommandEncoder::beginRenderPass() again.
   */
  void end();

  void bindProgram(const ProgramInfo* programInfo);
  void bindBuffers(GPUBuffer* indexBuffer, GPUBuffer* vertexBuffer, size_t vertexOffset = 0);
  void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount);
  void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount);

 protected:
  RenderPassDescriptor descriptor = {};
  std::shared_ptr<Program> program = nullptr;

  explicit RenderPass(RenderPassDescriptor descriptor) : descriptor(std::move(descriptor)) {
  }

  virtual bool onBindProgram(const ProgramInfo* programInfo) = 0;
  virtual bool onBindBuffers(GPUBuffer* indexBuffer, GPUBuffer* vertexBuffer,
                             size_t vertexOffset) = 0;
  virtual void onDraw(PrimitiveType primitiveType, size_t offset, size_t count,
                      bool drawIndexed) = 0;
  virtual void onEnd() = 0;

 private:
  enum class DrawPipelineStatus { Ok = 0, NotConfigured, FailedToBind };
  DrawPipelineStatus drawPipelineStatus = DrawPipelineStatus::NotConfigured;
  bool isEnd = false;

  friend class CommandEncoder;
};
}  // namespace tgfx
