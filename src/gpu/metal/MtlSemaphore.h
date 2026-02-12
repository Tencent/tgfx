/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <Metal/Metal.h>
#include "MtlResource.h"
#include "tgfx/gpu/Semaphore.h"

namespace tgfx {

class MtlGPU;

/**
 * Metal semaphore implementation using MTLEvent for GPU-to-GPU synchronization.
 */
class MtlSemaphore : public Semaphore, public MtlResource {
 public:
  /**
   * Creates a new Metal semaphore with an MTLEvent.
   */
  static std::shared_ptr<MtlSemaphore> Make(MtlGPU* gpu);

  /**
   * Creates a Metal semaphore from an existing MTLEvent.
   */
  static std::shared_ptr<MtlSemaphore> MakeFrom(MtlGPU* gpu, id<MTLEvent> event, uint64_t value);

  MtlSemaphore(id<MTLEvent> event, uint64_t value);
  ~MtlSemaphore() override = default;

  /**
   * Returns the MTLEvent used for synchronization.
   */
  id<MTLEvent> mtlEvent() const {
    return _event;
  }

  /**
   * Returns the signal value for the event.
   */
  uint64_t signalValue() const {
    return _value;
  }

  /**
   * Increments and returns the new signal value.
   */
  uint64_t nextSignalValue() {
    return ++_value;
  }

  BackendSemaphore getBackendSemaphore() const override;

 protected:
  void onRelease(MtlGPU* gpu) override;

 private:
  id<MTLEvent> _event = nil;
  uint64_t _value = 0;

  friend class MtlGPU;
};

}  // namespace tgfx
