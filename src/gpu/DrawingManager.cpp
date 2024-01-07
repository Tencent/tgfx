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
#include "gpu/tasks/TextureResolveTask.h"
#include "utils/Log.h"

namespace tgfx {
void DrawingManager::closeActiveOpsTask() {
  if (activeOpsTask) {
    activeOpsTask->makeClosed();
    activeOpsTask = nullptr;
  }
}

void DrawingManager::newTextureResolveRenderTask(
    std::shared_ptr<RenderTargetProxy> renderTargetProxy) {
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (renderTargetProxy->sampleCount() <= 1 && (!textureProxy || !textureProxy->hasMipmaps())) {
    return;
  }
  closeActiveOpsTask();
  auto task = std::make_shared<TextureResolveTask>(renderTargetProxy);
  task->makeClosed();
  tasks.push_back(std::move(task));
}

std::shared_ptr<OpsTask> DrawingManager::newOpsTask(
    std::shared_ptr<RenderTargetProxy> renderTargetProxy) {
  closeActiveOpsTask();
  auto opsTask = std::make_shared<OpsTask>(renderTargetProxy);
  tasks.push_back(opsTask);
  activeOpsTask = opsTask.get();
  return opsTask;
}

bool DrawingManager::flush() {
  if (tasks.empty()) {
    return false;
  }
  closeAllTasks();
  activeOpsTask = nullptr;
  std::vector<ResourceProxy*> proxies = {};
  std::for_each(tasks.begin(), tasks.end(),
                [&proxies](std::shared_ptr<RenderTask>& task) { task->gatherProxies(&proxies); });
  std::for_each(proxies.begin(), proxies.end(), [](ResourceProxy* proxy) {
    if (!(proxy->isInstantiated() || proxy->instantiate())) {
      LOGE("DrawingManager::flush() Failed to instantiate proxy!");
    }
  });
  auto gpu = context->gpu();
  std::for_each(tasks.begin(), tasks.end(),
                [gpu](std::shared_ptr<RenderTask>& task) { task->execute(gpu); });
  removeAllTasks();
  return true;
}

void DrawingManager::closeAllTasks() {
  for (auto& task : tasks) {
    task->makeClosed();
  }
}

void DrawingManager::removeAllTasks() {
  tasks.clear();
}
}  // namespace tgfx
