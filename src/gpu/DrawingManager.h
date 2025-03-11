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

#include <unordered_map>
#include <vector>
#include "gpu/OpsCompositor.h"
#include "gpu/tasks/OpsRenderTask.h"
#include "gpu/tasks/RenderTask.h"
#include "gpu/tasks/ResourceTask.h"
#include "gpu/tasks/TextureFlattenTask.h"

namespace tgfx {
class DrawingManager {
 public:
  explicit DrawingManager(Context* context);

  /**
   * Fills the render target using the provided fragment processor, and automatically resolves the
   * render target. Returns false if the render target or fragment processor is nullptr.
   */
  bool fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                    std::unique_ptr<FragmentProcessor> processor, uint32_t renderFlags);

  std::shared_ptr<OpsCompositor> addOpsCompositor(std::shared_ptr<RenderTargetProxy> renderTarget,
                                                  uint32_t renderFlags);

  void addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget, PlacementList<Op> ops);

  void addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                          std::vector<std::shared_ptr<TextureProxy>> inputs,
                          std::shared_ptr<RuntimeEffect> effect, const Point& offset);

  void addTextureResolveTask(std::shared_ptr<RenderTargetProxy> renderTarget);

  void addTextureFlattenTask(UniqueKey uniqueKey, std::shared_ptr<TextureProxy> textureProxy);

  void addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                               std::shared_ptr<TextureProxy> dest);

  void addResourceTask(PlacementNode<ResourceTask> resourceTask);

  /**
   * Returns true if any render tasks were executed.
   */
  bool flush();

 private:
  Context* context = nullptr;
  PlacementBuffer* drawingBuffer = nullptr;
  std::unique_ptr<RenderPass> renderPass = nullptr;
  PlacementList<ResourceTask> resourceTasks;
  PlacementList<TextureFlattenTask> flattenTasks;
  PlacementList<RenderTask> renderTasks;
  std::list<std::shared_ptr<OpsCompositor>> compositors = {};
  ResourceKeyMap<ResourceTask*> resourceTaskMap = {};

  friend class OpsCompositor;
};
}  // namespace tgfx
