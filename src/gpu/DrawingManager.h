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

#include <map>
#include <vector>
#include "gpu/OpsCompositor.h"
#include "gpu/tasks/AtlasUploadTask.h"
#include "gpu/tasks/OpsRenderTask.h"
#include "gpu/tasks/RenderTask.h"
#include "gpu/tasks/ResourceTask.h"

namespace tgfx {
struct RuntimeInputTexture;

struct AtlasCellData {
  std::shared_ptr<Data> pixels = nullptr;
  ImageInfo pixelsInfo = {};
  Point atlasOffset = {};
  AtlasCellData(std::shared_ptr<Data> data, const ImageInfo& info, const Point& offset)
      : pixels(std::move(data)), pixelsInfo(info), atlasOffset(offset) {
  }
};

class DrawingManager {
 public:
  explicit DrawingManager(Context* context);

  /**
   * Fills the render target using the provided fragment processor, and automatically resolves the
   * render target. Returns false if the render target or fragment processor is nullptr.
   */
  bool fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                    PlacementPtr<FragmentProcessor> processor, uint32_t renderFlags);

  std::shared_ptr<OpsCompositor> addOpsCompositor(std::shared_ptr<RenderTargetProxy> renderTarget,
                                                  uint32_t renderFlags,
                                                  std::optional<Color> clearColor = std::nullopt,
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
   * Flushes all recorded tasks and returns a CommandBuffer containing the GPU commands. If no tasks
   * were recorded, returns nullptr.
   */
  std::shared_ptr<CommandBuffer> flush();

 private:
  Context* context = nullptr;
  BlockAllocator* drawingAllocator = nullptr;
  std::vector<PlacementPtr<ResourceTask>> resourceTasks = {};
  std::vector<PlacementPtr<RenderTask>> renderTasks = {};
  std::list<std::shared_ptr<OpsCompositor>> compositors = {};
  std::map<TextureProxy*, PlacementPtr<AtlasUploadTask>> atlasTasks = {};

  friend class OpsCompositor;
};
}  // namespace tgfx
