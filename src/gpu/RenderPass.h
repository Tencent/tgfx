/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/ProgramInfo.h"
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
  virtual ~RenderPass() = default;

  Context* getContext() {
    return context;
  }

  std::shared_ptr<RenderTarget> renderTarget() {
    return _renderTarget;
  }

  std::shared_ptr<Texture> renderTargetTexture() {
    return _renderTargetTexture;
  }

  bool begin(std::shared_ptr<RenderTarget> renderTarget, std::shared_ptr<Texture> renderTexture);
  void end();
  void bindProgramAndScissorClip(const ProgramInfo* programInfo, const Rect& scissorRect);
  void bindBuffers(std::shared_ptr<GpuBuffer> indexBuffer, std::shared_ptr<GpuBuffer> vertexBuffer);
  void bindBuffers(std::shared_ptr<GpuBuffer> indexBuffer, std::shared_ptr<Data> vertexData);
  void draw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount);
  void drawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount);
  void clear(const Rect& scissor, Color color);
  void copyTo(Texture* texture, const Rect& srcRect, const Point& dstPoint);

 protected:
  explicit RenderPass(Context* context) : context(context) {
  }

  virtual void onBindRenderTarget() = 0;
  virtual bool onBindProgramAndScissorClip(const ProgramInfo* programInfo,
                                           const Rect& drawBounds) = 0;
  virtual void onDraw(PrimitiveType primitiveType, size_t baseVertex, size_t vertexCount) = 0;
  virtual void onDrawIndexed(PrimitiveType primitiveType, size_t baseIndex, size_t indexCount) = 0;
  virtual void onClear(const Rect& scissor, Color color) = 0;
  virtual void onCopyTo(Texture* texture, const Rect& srcRect, const Point& dstPoint) = 0;

  Context* context = nullptr;
  std::shared_ptr<RenderTarget> _renderTarget = nullptr;
  std::shared_ptr<Texture> _renderTargetTexture = nullptr;
  Program* _program = nullptr;
  std::shared_ptr<GpuBuffer> _indexBuffer = nullptr;
  std::shared_ptr<GpuBuffer> _vertexBuffer = nullptr;
  std::shared_ptr<Data> _vertexData = nullptr;

 private:
  enum class DrawPipelineStatus { Ok = 0, NotConfigured, FailedToBind };

  void resetActiveBuffers();

  DrawPipelineStatus drawPipelineStatus = DrawPipelineStatus::NotConfigured;
};
}  // namespace tgfx
