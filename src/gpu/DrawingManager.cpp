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
#include "gpu/tasks/RuntimeDrawTask.h"
#include "gpu/tasks/TextureResolveTask.h"

namespace tgfx {
std::shared_ptr<OpsRenderTask> DrawingManager::addOpsTask(std::shared_ptr<RenderTargetProxy> target,
                                                          uint32_t renderFlags) {
  checkIfResolveNeeded(target);
  auto opsTask = std::make_shared<OpsRenderTask>(std::move(target), renderFlags);
  addRenderTask(opsTask);
  activeOpsTask = opsTask;
  return opsTask;
}

void DrawingManager::addRuntimeDrawTask(std::shared_ptr<RenderTargetProxy> target,
                                        std::vector<std::shared_ptr<TextureProxy>> inputs,
                                        std::shared_ptr<RuntimeEffect> effect,
                                        const Point& offset) {
  if (target == nullptr || inputs.empty() || effect == nullptr) {
    return;
  }
  checkIfResolveNeeded(target);
  auto task = std::make_shared<RuntimeDrawTask>(std::move(target), std::move(inputs),
                                                std::move(effect), offset);
  addRenderTask(std::move(task));
}

void DrawingManager::addTextureFlattenTask(std::shared_ptr<TextureFlattenTask> flattenTask) {
  if (flattenTask == nullptr) {
    return;
  }
  flattenTasks.push_back(std::move(flattenTask));
}

void DrawingManager::addTextureResolveTask(std::shared_ptr<RenderTargetProxy> target) {
  auto textureProxy = target->getTextureProxy();
  if (textureProxy == nullptr || (target->sampleCount() <= 1 && !textureProxy->hasMipmaps())) {
    return;
  }
  // TODO(domchen): Skip resolving if the render target is not in the needResolveTargets set.
  needResolveTargets.erase(target);
  auto task = std::make_shared<TextureResolveTask>(std::move(target));
  addRenderTask(std::move(task));
}

void DrawingManager::addRenderTargetCopyTask(std::shared_ptr<RenderTargetProxy> source,
                                             std::shared_ptr<TextureProxy> dest, Rect srcRect,
                                             Point dstPoint) {
  if (source == nullptr || dest == nullptr) {
    return;
  }
  auto task =
      std::make_shared<RenderTargetCopyTask>(std::move(source), std::move(dest), srcRect, dstPoint);
  addRenderTask(std::move(task));
}

void DrawingManager::addResourceTask(std::shared_ptr<ResourceTask> resourceTask) {
  if (resourceTask == nullptr) {
    return;
  }
#ifdef DEBUG
  auto result = resourceTaskMap.find(resourceTask->uniqueKey);
  DEBUG_ASSERT(result == resourceTaskMap.end());
  resourceTaskMap[resourceTask->uniqueKey] = resourceTask.get();
#endif
  resourceTasks.push_back(std::move(resourceTask));
}

bool DrawingManager::flush() {
  if (resourceTasks.empty() && renderTasks.empty()) {
    return false;
  }
  if (activeOpsTask) {
    activeOpsTask->makeClosed();
    activeOpsTask = nullptr;
  }

  for (auto& task : renderTasks) {
    task->prepare(context);
  }
  for (auto& task : resourceTasks) {
    task->execute(context);
  }
  resourceTasks = {};
#ifdef DEBUG
  resourceTaskMap = {};
#endif

  std::vector<std::shared_ptr<TextureFlattenTask>> validFlattenTasks = {};
  for (auto& task : flattenTasks) {
    if (task->prepare(context)) {
      validFlattenTasks.push_back(task);
    }
  }
  for (auto& task : validFlattenTasks) {
    task->execute(context);
  }
  flattenTasks = {};

  for (auto& renderTarget : needResolveTargets) {
    auto task = std::make_shared<TextureResolveTask>(renderTarget);
    renderTasks.push_back(std::move(task));
  }
  needResolveTargets = {};

  for (auto& task : renderTasks) {
    task->execute(context->gpu());
  }
  renderTasks = {};
  return true;
}

void DrawingManager::addRenderTask(std::shared_ptr<RenderTask> renderTask) {
  if (activeOpsTask) {
    activeOpsTask->makeClosed();
    activeOpsTask = nullptr;
  }
  renderTasks.push_back(std::move(renderTask));
}

void DrawingManager::checkIfResolveNeeded(std::shared_ptr<RenderTargetProxy> renderTargetProxy) {
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (textureProxy == nullptr ||
      (renderTargetProxy->sampleCount() <= 1 && !textureProxy->hasMipmaps())) {
    return;
  }
  needResolveTargets.insert(std::move(renderTargetProxy));
}
}  // namespace tgfx
