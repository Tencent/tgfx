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

#include <memory>
#include "gpu/CommandBuffer.h"

namespace tgfx {
/**
 * CommandQueue is an interface for managing the execution of encoded commands on the GPU.
 * The primary queue can be accessed via the GPU::queue() method.
 */
class CommandQueue {
 public:
  virtual ~CommandQueue() = default;

  /**
   * Schedules the execution of the specified command buffer on the GPU.
   */
  virtual void submit(std::shared_ptr<CommandBuffer> commandBuffer) = 0;

  /**
   * Blocks the current thread until all previously submitted commands in this queue have
   * completed execution on the GPU.
   */
  virtual void waitUntilCompleted() = 0;
};
}  // namespace tgfx
