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

#include "gpu/GPUResource.h"
#include "tgfx/gpu/Backend.h"

namespace tgfx {
/**
 * GPUFence is a synchronization primitive to capture, track, and manager resource dependencies
 * across command encoders.
 */
class GPUFence : public GPUResource {
 public:
  /**
   * Returns the backend semaphore object.
   */
  virtual BackendSemaphore getBackendSemaphore() const = 0;
};
}  // namespace tgfx
