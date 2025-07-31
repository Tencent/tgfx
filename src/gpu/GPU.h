/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "gpu/CommandEncoder.h"
#include "gpu/CommandQueue.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {
/**
 * This is the main interface for accessing GPU functionality. In Metal, Vulkan, and WebGPU, its
 * equivalents are MTLDevice, VkDevice, and GPUDevice. For OpenGL, it simply refers to GL functions.
 */
class GPU {
 public:
  virtual ~GPU() = default;

  /**
   * Returns the backend type of the GPU.
   */
  virtual Backend backend() const = 0;

  /**
   * Returns the capability info of the GPU.
   */
  virtual const Caps* caps() const = 0;

  /**
   * Returns the primary CommandQueue associated with this GPU.
   */
  virtual CommandQueue* queue() const = 0;

  /**
   * Creates a command encoder that can be used to encode commands to be issued to the GPU.
   */
  virtual std::shared_ptr<CommandEncoder> createCommandEncoder() const = 0;
};
}  // namespace tgfx
