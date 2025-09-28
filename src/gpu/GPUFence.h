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

#include "tgfx/gpu/Backend.h"

namespace tgfx {
/**
 * GPUFence is a synchronization primitive used for both CPU-GPU and GPU-GPU operations. It has two
 * states: signaled and unsignaled. A fence becomes signaled when a queue submission finishes. Once
 * signaled, the fence remains in that state and cannot be reset; create a new fence for further
 * synchronization.
 */
class GPUFence {
 public:
  virtual ~GPUFence() = default;

  /**
   * Returns true if the fence is signaled; otherwise, returns false. This check does not block.
   */
  virtual bool isSignaled() const = 0;

  /**
   * Waits on the CPU until the fence is signaled.
   */
  virtual void waitOnCPU() = 0;

  /**
   * Returns the backend semaphore object and transfers ownership to the caller, who must manage its
   * lifetime. If the fence is invalid, an empty semaphore is returned.
   */
  virtual BackendSemaphore stealBackendSemaphore() = 0;
};
}  // namespace tgfx
