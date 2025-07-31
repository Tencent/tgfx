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

#include "gpu/GPUBuffer.h"
#include "gpu/Pipeline.h"
#include "gpu/RenderTarget.h"
#include "tgfx/core/Color.h"
#include "tgfx/gpu/Context.h"

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

  Context* getContext() {
    return renderTarget->getContext();
  }

  std::shared_ptr<RenderTarget> getRenderTarget() {
    return renderTarget;
  }

  void end();
  void bindProgramAndScissorClip(const Pipeline* pipeline, const Rect& scissorRect);
  void bindBuffers(std::shared_ptr<GPUBuffer> indexBuffer, std::shared_ptr<GPUBuffer> vertexBuffer,
                   size_t vertexOffset = 0);
  void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount);
  void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount);
  void clear(const Rect& scissor, Color color);

 protected:
  explicit RenderPass(std::shared_ptr<RenderTarget> renderTarget)
      : renderTarget(std::move(renderTarget)) {
  }

  virtual bool onBindProgramAndScissorClip(const Pipeline* pipeline, const Rect& drawBounds) = 0;
  virtual bool onBindBuffers(std::shared_ptr<GPUBuffer> indexBuffer,
                             std::shared_ptr<GPUBuffer> vertexBuffer, size_t vertexOffset) = 0;
  virtual void onDraw(PrimitiveType primitiveType, size_t offset, size_t count,
                      bool drawIndexed) = 0;
  virtual void onClear(const Rect& scissor, Color color) = 0;
  virtual void onEnd() = 0;

  std::shared_ptr<RenderTarget> renderTarget = nullptr;
  std::shared_ptr<Program> program = nullptr;

 private:
  enum class DrawPipelineStatus { Ok = 0, NotConfigured, FailedToBind };
  DrawPipelineStatus drawPipelineStatus = DrawPipelineStatus::NotConfigured;
  bool isEnd = false;

  friend class CommandEncoder;
};
}  // namespace tgfx
