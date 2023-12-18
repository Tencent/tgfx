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
#include "TextureProxy.h"
#include "TextureResolveRenderTask.h"
#include "gpu/Gpu.h"
#include "gpu/RenderTarget.h"
#include "utils/Log.h"

namespace tgfx {
void DrawingManager::closeActiveOpsTask() {
  if (activeOpsTask) {
    activeOpsTask->makeClosed();
    activeOpsTask = nullptr;
  }
}

void DrawingManager::newTextureResolveRenderTask(Surface* surface) {
  auto texture = surface->texture;
  if (surface->renderTarget->sampleCount() <= 1 &&
      (!texture || !texture->getSampler()->hasMipmaps())) {
    return;
  }
  closeActiveOpsTask();
  auto task = std::make_shared<TextureResolveRenderTask>(surface->renderTarget, surface->texture);
  task->makeClosed();
  tasks.push_back(std::move(task));
}

std::shared_ptr<OpsTask> DrawingManager::newOpsTask(Surface* surface) {
  closeActiveOpsTask();
  auto opsTask = std::make_shared<OpsTask>(surface->renderTarget, surface->texture);
  tasks.push_back(opsTask);
  activeOpsTask = opsTask.get();
  return opsTask;
}

bool DrawingManager::flush(Semaphore* signalSemaphore) {
  auto* gpu = context->gpu();
  closeAllTasks();
  activeOpsTask = nullptr;
  std::vector<TextureProxy*> proxies;
  std::for_each(tasks.begin(), tasks.end(),
                [&proxies](std::shared_ptr<RenderTask>& task) { task->gatherProxies(&proxies); });
  std::for_each(proxies.begin(), proxies.end(), [](TextureProxy* proxy) {
    if (!(proxy->isInstantiated() || proxy->instantiate())) {
      LOGE("Failed to instantiate texture from proxy.");
    }
  });
  std::for_each(tasks.begin(), tasks.end(),
                [gpu](std::shared_ptr<RenderTask>& task) { task->execute(gpu); });
  removeAllTasks();
  return context->caps()->semaphoreSupport && gpu->insertSemaphore(signalSemaphore);
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
