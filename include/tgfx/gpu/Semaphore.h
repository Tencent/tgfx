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
 * Semaphore is a synchronization primitive for GPU-to-GPU operations. Once a Semaphore is signaled,
 * it remains in that state and cannot be reset; to synchronize again, create a new Semaphore.
 */
class Semaphore {
 public:
  virtual ~Semaphore() = default;

  /**
   * Returns a BackendSemaphore representing the underlying backend-specific semaphore.
   */
  virtual BackendSemaphore getBackendSemaphore() const = 0;
};
}  // namespace tgfx
