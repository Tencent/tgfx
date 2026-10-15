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

  void addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                        PlacementArray<DrawOp> drawOps, std::optional<PMColor> clearColor);

  void addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                          std::vector<RuntimeInputTexture> inputs,
                          std::shared_ptr<RuntimeEffect> effect, const Point& offset);

  void addGenerateMipmapsTask(std::shared_ptr<TextureProxy> textureProxy);

  void addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                               std::shared_ptr<TextureProxy> dest, int srcX = 0, int srcY = 0);

  void addTransferPixelsTask(std::shared_ptr<RenderTargetProxy> source, const Rect& srcRect,
                             std::shared_ptr<GPUBufferProxy> dest);

  void addResourceTask(PlacementPtr<ResourceTask> resourceTask);

  void addAtlasCellTask(std::shared_ptr<TextureProxy> textureProxy, const Point& atlasOffset,
                        std::shared_ptr<ImageCodec> codec);

  /**
   * Records that the current batch uses the window frame behind the render target, without
   * requesting its presentation.
   */
  void collectFrameUsage(std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns the target for a new Surface of the window: the current target if this batch already
   * uses it, otherwise a newly created one that becomes current.
   */
  std::shared_ptr<RenderTargetProxy> createWindowTarget(std::shared_ptr<Window> window);

  /**
   * Returns the window's current target, or makes renderTarget current if there is none.
   */
  std::shared_ptr<RenderTargetProxy> resolveWindowTarget(
      const std::shared_ptr<Window>& window, std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns true if the current batch uses the frame behind the render target.
   */
  bool isFrameUsed(const std::shared_ptr<RenderTargetProxy>& renderTarget) const;

  /**
   * Closes all open compositors drawing into the render target, keeping the recording order of
   * surfaces that share it.
   */
  void closeCompositors(const std::shared_ptr<RenderTargetProxy>& renderTarget);

  /**
   * Requests presenting the window's render target once at the end of the current batch.
   */
  void collectWindow(std::shared_ptr<Window> window,
                     std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns true if the current batch presents the render target.
   */
  bool hasPendingPresentation(const std::shared_ptr<RenderTargetProxy>& renderTarget) const;

  /**
   * Flushes all pending drawing operations and returns the DrawingBuffer. Returns nullptr if there
   * are no pending drawing operations. The returned DrawingBuffer will be automatically recycled
   * to the pool after it's no longer referenced.
   */
  std::shared_ptr<DrawingBuffer> flush();

  /**
   * Takes the windows collected by the last flush().
   */
  std::vector<std::shared_ptr<Window>> takePendingWindows() {
    return std::move(pendingWindows);
  }

 private:
  struct WindowTarget {
    std::weak_ptr<Window> window;
    std::weak_ptr<RenderTargetProxy> target;
  };

  // The current target of each window, held weakly; expired entries are dropped at flush time.
  std::unordered_map<Window*, WindowTarget> windowTargets = {};

  // Windows presented by the current batch, moved into the Recording at flush time to keep them
  // alive until submission.
  std::vector<std::shared_ptr<Window>> pendingWindows = {};
  Context* context = nullptr;
  std::shared_ptr<DrawingBuffer> currentBuffer = nullptr;
  std::deque<std::shared_ptr<DrawingBuffer>> bufferPool = {};
  std::list<std::shared_ptr<OpsCompositor>> compositors = {};
  std::unordered_map<TextureProxy*, AtlasUploadTask*> atlasTaskMap = {};

  DrawingBuffer* getDrawingBuffer() {
    return currentBuffer ? currentBuffer.get() : createDrawingBuffer();
  }

  DrawingBuffer* createDrawingBuffer();

  WindowTarget& windowTargetEntry(const std::shared_ptr<Window>& window);

  friend class OpsCompositor;
  friend class RenderContext;
};
}  // namespace tgfx
