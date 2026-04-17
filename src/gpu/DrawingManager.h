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

#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>
#include "gpu/DrawingBuffer.h"
#include "gpu/OpsCompositor.h"
#include "gpu/tasks/AtlasUploadTask.h"
#include "gpu/tasks/OpsRenderTask.h"
#include "gpu/tasks/RenderTask.h"
#include "gpu/tasks/ResourceTask.h"

namespace tgfx {
struct RuntimeInputTexture;

class DrawingManager {
 public:
  explicit DrawingManager(Context* context);

  BlockAllocator* drawingAllocator() {
    return &getDrawingBuffer()->drawingAllocator;
  }

  BlockAllocator* vertexAllocator() {
    return &getDrawingBuffer()->vertexAllocator;
  }

  BlockAllocator* instanceAllocator() {
    return &getDrawingBuffer()->instanceAllocator;
  }

  /**
   * Fills the render target using the provided fragment processor, and automatically resolves the
   * render target. Returns false if the render target or fragment processor is nullptr.
   */
  bool fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                    PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags);

  std::shared_ptr<OpsCompositor> addOpsCompositor(std::shared_ptr<RenderTargetProxy> renderTarget,
                                                  uint32_t renderFlags,
                                                  std::optional<PMColor> clearColor = std::nullopt,
                                                  std::shared_ptr<ColorSpace> colorSpace = nullptr);

  /**
   * Adds a render task to execute the provided draw operations on the render target.
   * @param renderTarget The render target to render to.
   * @param drawOps The draw operations to execute.
   * @param clearColor The optional clear color. If set, the render target will be cleared before
   * drawing.
   * @param resolveOnPassEnd If true, MSAA content will be resolved when the RenderPass ends. This
   * should be used for auxiliary render targets (like clip masks) that will be used immediately
   * after rendering. If false (default), MSAA resolve is deferred and will be triggered by
   * ensureMSAAResolved() when the content needs to be read.
   */
  void addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                        PlacementArray<DrawOp> drawOps, std::optional<PMColor> clearColor,
                        bool resolveOnPassEnd = false);

  void addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                          std::vector<RuntimeInputTexture> inputs,
                          std::shared_ptr<RuntimeEffect> effect, const Point& offset);

  void addGenerateMipmapsTask(std::shared_ptr<TextureProxy> textureProxy);

  void addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                               std::shared_ptr<TextureProxy> dest, int srcX = 0, int srcY = 0);

  void addTransferPixelsTask(std::shared_ptr<RenderTargetProxy> source, const Rect& srcRect,
                             std::shared_ptr<GPUBufferProxy> dest);

  /**
   * Ensures the MSAA render target is resolved before reading its content. Checks dirty state,
   * resolves if needed, and clears the dirty rect. Call this before any operation that reads from
   * the render target's sample texture.
   */
  void ensureMSAAResolved(const std::shared_ptr<RenderTargetProxy>& renderTarget);

  void addResourceTask(PlacementPtr<ResourceTask> resourceTask);

  void addAtlasCellTask(std::shared_ptr<TextureProxy> textureProxy, const Point& atlasOffset,
                        std::shared_ptr<ImageCodec> codec);

  /**
   * Collects a Window that needs to be presented after command buffer submission.
   */
  void collectWindow(std::weak_ptr<Window> window);

  /**
   * Flushes all pending drawing operations and returns the DrawingBuffer. Returns nullptr if there
   * are no pending drawing operations. The returned DrawingBuffer will be automatically recycled
   * to the pool after it's no longer referenced.
   */
  std::shared_ptr<DrawingBuffer> flush();

 private:
  Context* context = nullptr;
  std::shared_ptr<DrawingBuffer> currentBuffer = nullptr;
  std::deque<std::shared_ptr<DrawingBuffer>> bufferPool = {};
  std::list<std::shared_ptr<OpsCompositor>> compositors = {};
  std::unordered_map<TextureProxy*, AtlasUploadTask*> atlasTaskMap = {};
  // Tracks the number of dirty MSAA render targets in this context. When zero,
  // addOpsRenderTask() skips FP tree traversal for zero overhead in non-MSAA scenarios.
  int dirtyMSAARTCount = 0;

  DrawingBuffer* getDrawingBuffer() {
    return currentBuffer ? currentBuffer.get() : createDrawingBuffer();
  }

  DrawingBuffer* createDrawingBuffer();

  void addResolveMSAATask(std::shared_ptr<RenderTargetProxy> renderTarget, const Rect& resolveRect);

  friend class OpsCompositor;
  friend class RenderContext;
};
}  // namespace tgfx
