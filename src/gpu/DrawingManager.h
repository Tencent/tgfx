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
#include <unordered_set>
#include <vector>
#include "gpu/tasks/OpsRenderTask.h"
#include "gpu/tasks/RenderTask.h"
#include "gpu/tasks/ResourceTask.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
class DrawingManager {
 public:
  explicit DrawingManager(Context* context) : context(context) {
  }

  std::shared_ptr<OpsRenderTask> addOpsTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy);

  void addTextureResolveTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy);

  void addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                               std::shared_ptr<TextureProxy> dest, Rect srcRect, Point dstPoint);

  void addResourceTask(std::shared_ptr<ResourceTask> resourceTask);

  /**
   * Returns true if any render tasks were executed.
   */
  bool flush();

 private:
  void closeActiveOpsTask();

  Context* context = nullptr;
  ResourceKeyMap<ResourceTask*> resourceTaskMap = {};
  std::vector<std::shared_ptr<ResourceTask>> resourceTasks = {};
  std::vector<std::shared_ptr<RenderTask>> renderTasks = {};
  OpsRenderTask* activeOpsTask = nullptr;
};
}  // namespace tgfx
