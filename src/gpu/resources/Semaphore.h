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
#include "gpu/GPUFence.h"
#include "gpu/resources/Resource.h"

namespace tgfx {
/**
 * Wrapper class for a GPUFence object.
 */
class Semaphore : public Resource {
 public:
  /**
   * Wraps a backend semaphore object into a Semaphore instance and takes ownership of it.
   */
  static std::shared_ptr<Semaphore> MakeAdopted(Context* context,
                                                const BackendSemaphore& backendSemaphore);

  size_t memoryUsage() const override {
    return 0;
  }

  /**
   * Returns the GPUFence object associated with this Semaphore instance.
   */
  GPUFence* getFence() {
    return fence.get();
  }

  /**
   * Returns the backend semaphore object associated with this Semaphore instance.
   */
  BackendSemaphore getBackendSemaphore() const {
    return fence->getBackendSemaphore();
  }

 protected:
  void onReleaseGPU() override {
    fence->release(context->gpu());
  }

 private:
  std::unique_ptr<GPUFence> fence = nullptr;

  explicit Semaphore(std::unique_ptr<GPUFence> fence) : fence(std::move(fence)) {
  }
};
}  // namespace tgfx
