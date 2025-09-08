/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "RenderTask.h"
#include "gpu/Semaphore.h"

namespace tgfx {
class SemaphoreWaitTask : public RenderTask {
 public:
  explicit SemaphoreWaitTask(std::shared_ptr<Semaphore> semaphore)
      : semaphore(std::move(semaphore)) {
  }

  void execute(CommandEncoder* encoder) override {
    encoder->waitForFence(semaphore->getFence());
  }

 private:
  std::shared_ptr<Semaphore> semaphore = nullptr;
};
}  // namespace tgfx
