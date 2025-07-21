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

#include "Program.h"
#include "gpu/Gpu.h"
#include "gpu/ProgramCreator.h"
#include "gpu/RenderTarget.h"
#include "gpu/processors/GeometryProcessor.h"
#include "gpu/proxies/GpuBufferProxy.h"
#include "gpu/proxies/RenderTargetProxy.h"
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
  static std::unique_ptr<RenderPass> Make(Context* context);

  virtual ~RenderPass() = default;

  Context* getContext() {
    return context;
  }

  std::shared_ptr<RenderTarget> renderTarget() {
    return _renderTarget;
  }

  bool begin(std::shared_ptr<RenderTarget> renderTarget);
  void end();
  void bindProgramAndScissorClip(const Pipeline* pipeline, const Rect& scissorRect);
  void bindBuffers(std::shared_ptr<GpuBuffer> indexBuffer, std::shared_ptr<GpuBuffer> vertexBuffer,
                   size_t vertexOffset = 0);
  void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount);
  void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount);
  void clear(const Rect& scissor, Color color);
  void resolve(const Rect& bounds);
  void copyToTexture(Texture* texture, int srcX, int srcY);

 protected:
  explicit RenderPass(Context* context) : context(context) {
  }

  virtual void onBindRenderTarget() = 0;
  virtual void onUnbindRenderTarget() = 0;
  virtual bool onBindProgramAndScissorClip(const Pipeline* pipeline, const Rect& drawBounds) = 0;
  virtual bool onBindBuffers(std::shared_ptr<GpuBuffer> indexBuffer,
                             std::shared_ptr<GpuBuffer> vertexBuffer, size_t vertexOffset) = 0;
  virtual void onDraw(PrimitiveType primitiveType, size_t offset, size_t count,
                      bool drawIndexed) = 0;
  virtual void onClear(const Rect& scissor, Color color) = 0;
  virtual void onCopyToTexture(Texture* texture, int srcX, int srcY) = 0;

  Context* context = nullptr;
  std::shared_ptr<RenderTarget> _renderTarget = nullptr;
  Program* _program = nullptr;

 private:
  enum class DrawPipelineStatus { Ok = 0, NotConfigured, FailedToBind };

  DrawPipelineStatus drawPipelineStatus = DrawPipelineStatus::NotConfigured;
};
}  // namespace tgfx
