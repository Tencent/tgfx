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

#include "MetalSemaphore.h"
#include "MetalGPU.h"

namespace tgfx {

std::shared_ptr<MetalSemaphore> MetalSemaphore::Make(MetalGPU* gpu) {
  if (gpu == nullptr) {
    return nullptr;
  }
  
  id<MTLEvent> event = [gpu->device() newEvent];
  if (event == nil) {
    return nullptr;
  }
  
  return gpu->makeResource<MetalSemaphore>(event, static_cast<uint64_t>(0));
}

std::shared_ptr<MetalSemaphore> MetalSemaphore::MakeFrom(MetalGPU* gpu, id<MTLEvent> event,
                                                     uint64_t value) {
  if (gpu == nullptr || event == nil) {
    return nullptr;
  }
  [event retain];
  return gpu->makeResource<MetalSemaphore>(event, value);
}

MetalSemaphore::MetalSemaphore(id<MTLEvent> event, uint64_t value)
    : _event(event), _value(value) {
}

BackendSemaphore MetalSemaphore::getBackendSemaphore() const {
  if (_event == nil) {
    return {};
  }
  MetalSemaphoreInfo metalInfo = {};
  metalInfo.event = (__bridge const void*)_event;
  metalInfo.value = _value;
  return BackendSemaphore(metalInfo);
}

void MetalSemaphore::onRelease(MetalGPU*) {
  if (_event != nil) {
    [_event release];
    _event = nil;
  }
}

}  // namespace tgfx
