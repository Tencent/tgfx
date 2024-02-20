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

#include "DrawingManager.h"
#include "gpu/Gpu.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/tasks/RenderTargetCopyTask.h"
#include "gpu/tasks/TextureResolveTask.h"
#include "utils/Log.h"

namespace tgfx {
void DrawingManager::closeActiveOpsTask() {
  if (activeOpsTask) {
    activeOpsTask->makeClosed();
    activeOpsTask = nullptr;
  }
}

std::shared_ptr<OpsRenderTask> DrawingManager::addOpsTask(
    std::shared_ptr<RenderTargetProxy> renderTargetProxy) {
  closeActiveOpsTask();
  auto opsTask = std::make_shared<OpsRenderTask>(renderTargetProxy);
  renderTasks.push_back(opsTask);
  activeOpsTask = opsTask.get();
  return opsTask;
}

void DrawingManager::addTextureResolveTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy) {
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (textureProxy == nullptr ||
      (renderTargetProxy->sampleCount() <= 1 && !textureProxy->hasMipmaps())) {
    return;
  }
  closeActiveOpsTask();
  auto task = std::make_shared<TextureResolveTask>(renderTargetProxy);
  task->makeClosed();
  renderTasks.push_back(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest, Rect srcRect,
                                             Point dstPoint) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  closeActiveOpsTask();
  auto task = std::make_shared<RenderTargetCopyTask>(source, dest, srcRect, dstPoint);
  task->makeClosed();
  renderTasks.push_back(std::move(task));
}

void DrawingManager::addResourceTask(std::shared_ptr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
  auto result = resourceTaskMap.find(resourceTask->uniqueKey.domain());
  if (result != resourceTaskMap.end()) {
    // Remove the UniqueKey from the old task, so it will be skipped when the task is executed.
    result->second->uniqueKey = {};
  }
  resourceTaskMap[resourceTask->uniqueKey.domain()] = resourceTask.get();
  resourceTasks.push_back(std::move(resourceTask));
}

bool DrawingManager::flush() {
  if (resourceTasks.empty() && renderTasks.empty()) {
    return false;
  }
  for (auto& task : renderTasks) {
    task->prepare(context);
  }
  for (auto& task : resourceTasks) {
    task->execute(context);
  }
  resourceTaskMap = {};
  resourceTasks = {};
  for (auto& task : renderTasks) {
    task->makeClosed();
  }
  activeOpsTask = nullptr;
  for (auto& task : renderTasks) {
    task->execute(context->gpu());
  }
  renderTasks = {};
  return true;
}
}  // namespace tgfx
