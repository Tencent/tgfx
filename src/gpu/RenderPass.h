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

class RenderPass {
 public:
  virtual ~RenderPass() = default;

  void end();
  void bindProgramAndScissorClip(const ProgramInfo* programInfo, const Rect& scissorRect);
  void bindBuffers(GPUBuffer* indexBuffer, GPUBuffer* vertexBuffer, size_t vertexOffset = 0);
  void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount);
  void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount);

 protected:
  RenderPassDescriptor descriptor = {};
  std::shared_ptr<Program> program = nullptr;

  explicit RenderPass(RenderPassDescriptor descriptor) : descriptor(std::move(descriptor)) {
  }

  virtual bool onBindProgramAndScissorClip(const ProgramInfo* programInfo,
                                           const Rect& drawBounds) = 0;
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
