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
  explicit DrawingManager(Context* context) : context(context) {
  }

  /**
   * Fills the render target using the provided fragment processor, and automatically resolves the
   * render target. Returns false if the render target or fragment processor is nullptr.
   */
  bool fillRTWithFP(std::shared_ptr<RenderTargetProxy> renderTarget,
                    std::unique_ptr<FragmentProcessor> processor, uint32_t renderFlags);

  std::shared_ptr<OpsCompositor> addOpsCompositor(std::shared_ptr<RenderTargetProxy> renderTarget,
                                                  uint32_t renderFlags);

  void addOpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                        std::vector<std::unique_ptr<Op>> ops);

  void addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> renderTarget,
                          std::vector<std::shared_ptr<TextureProxy>> inputs,
                          std::shared_ptr<RuntimeEffect> effect, const Point& offset);

  void addTextureResolveTask(std::shared_ptr<RenderTargetProxy> renderTarget);

  void addTextureFlattenTask(std::shared_ptr<TextureFlattenTask> flattenTask);

  void addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                               std::shared_ptr<TextureProxy> dest, Rect srcRect, Point dstPoint);

  void addResourceTask(std::shared_ptr<ResourceTask> resourceTask);

  /**
   * Returns true if any render tasks were executed.
   */
  bool flush();

 private:
  Context* context = nullptr;
  std::vector<std::shared_ptr<ResourceTask>> resourceTasks = {};
  std::vector<std::shared_ptr<TextureFlattenTask>> flattenTasks = {};
  std::vector<std::shared_ptr<RenderTask>> renderTasks = {};
  std::vector<std::shared_ptr<OpsCompositor>> compositors = {};
  ResourceKeyMap<size_t> resourceTaskMap = {};

  friend class RenderQueue;
};
}  // namespace tgfx
