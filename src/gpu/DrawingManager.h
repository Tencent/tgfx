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

#include <vector>
#include "gpu/tasks/OpsTask.h"
#include "gpu/tasks/RenderTask.h"
#include "tgfx/gpu/Surface.h"

namespace tgfx {
class DrawingManager {
 public:
  explicit DrawingManager(Context* context) : context(context) {
  }

  std::shared_ptr<OpsTask> newOpsTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy);

  bool flush(Semaphore* signalSemaphore);

  void newTextureResolveRenderTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy);

 private:
  void closeAllTasks();

  void removeAllTasks();

  void closeActiveOpsTask();

  Context* context = nullptr;
  std::vector<std::shared_ptr<RenderTask>> tasks;
  OpsTask* activeOpsTask = nullptr;

  friend class Surface;
};
}  // namespace tgfx
