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
   * Records that the current drawing batch uses the window frame behind the given render target,
   * so the submission wires the frame acquisition synchronization. Called by window surfaces
   * whenever they record work on their target; the drawing manager does not inspect where a
   * render target comes from. Recording a usage never requests a presentation.
   */
  void collectFrameUsage(std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns the render target for a new Surface of the given window
   * (Surface::MakeFrom(context, window)). If the current batch already uses the window's frame,
   * returns the window's current target so the new surface joins that frame. Otherwise creates a
   * fresh target (re-reading the window size and rebuilding expired presentation resources) and
   * makes it the window's current target; surfaces holding an older target switch to it the next
   * time they use the window (see resolveWindowTarget()).
   */
  std::shared_ptr<RenderTargetProxy> createWindowTarget(std::shared_ptr<Window> window);

  /**
   * Returns the window's current target that a window surface holding renderTarget must use for
   * the current batch, so all surfaces of a window use one frame per batch whether they were
   * created in this batch or earlier. If no live surface holds a current target for the window,
   * renderTarget becomes the current target and is returned. Called by window surfaces before
   * they record work on their frame; the caller decides whether it can switch to the result.
   */
  std::shared_ptr<RenderTargetProxy> resolveWindowTarget(
      const std::shared_ptr<Window>& window, std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns true if the current, not yet flushed drawing batch uses the frame behind the given
   * render target (see collectFrameUsage()).
   */
  bool isFrameUsed(const std::shared_ptr<RenderTargetProxy>& renderTarget) const;

  /**
   * Closes every open ops compositor that draws into the given render target, appending their
   * pending drawing to the current batch. Called by window surfaces, whose surfaces share one
   * target per batch, before they record new work on it, so the batch's tasks keep the order
   * in which the surfaces' drawing and copies were recorded.
   */
  void closeCompositors(const std::shared_ptr<RenderTargetProxy>& renderTarget);

  /**
   * Requests the presentation of the given window's render target at the end of the current
   * recording batch (called by window surfaces when they draw). Each batch presents a window
   * target once: the target's proxy carries the backend presentation and is delivered after the
   * batch is submitted.
   */
  void collectWindow(std::shared_ptr<Window> window,
                     std::shared_ptr<RenderTargetProxy> renderTarget);

  /**
   * Returns true if the current, not yet flushed drawing batch presents the given render target,
   * that is, the target's frame has been drawn in this batch, so work recorded now (such as a
   * readback copy) still joins the batch that draws and presents the frame.
   */
  bool hasPendingPresentation(const std::shared_ptr<RenderTargetProxy>& renderTarget) const;

  /**
   * Flushes all pending drawing operations and returns the DrawingBuffer. Returns nullptr if there
   * are no pending drawing operations. The returned DrawingBuffer will be automatically recycled
   * to the pool after it's no longer referenced.
   */
  std::shared_ptr<DrawingBuffer> flush();

  /**
   * Takes the windows whose frames the flushed buffer collected (called right after flush()).
   */
  std::vector<std::shared_ptr<Window>> takePendingWindows() {
    return std::move(pendingWindows);
  }

 private:
  struct WindowTarget {
    std::weak_ptr<Window> window;
    std::weak_ptr<RenderTargetProxy> target;
  };

  // The current render target of each window (see createWindowTarget() and
  // resolveWindowTarget()). Held weakly: the surfaces own their targets, and an entry whose
  // target is no longer held by any surface is dropped at flush time.
  std::unordered_map<Window*, WindowTarget> windowTargets = {};

  // The windows whose presentations the current drawing buffer registered. The presentations
  // hold them weakly (through the proxies), so the strong references are moved into the
  // Recording at flush time: while the Recording is alive, shared-backbuffer backends can still
  // present through the window when the Recording is submitted after the surfaces and windows
  // themselves are gone.
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
