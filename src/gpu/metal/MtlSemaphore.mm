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

#include "MtlSemaphore.h"
#include "MtlGPU.h"

namespace tgfx {

std::shared_ptr<MtlSemaphore> MtlSemaphore::Make(MtlGPU* gpu) {
  if (gpu == nullptr) {
    return nullptr;
  }
  
  id<MTLEvent> event = [gpu->device() newEvent];
  if (event == nil) {
    return nullptr;
  }
  
  return gpu->makeResource<MtlSemaphore>(event, static_cast<uint64_t>(0));
}

std::shared_ptr<MtlSemaphore> MtlSemaphore::MakeFrom(MtlGPU* gpu, id<MTLEvent> event,
                                                     uint64_t value) {
  if (gpu == nullptr || event == nil) {
    return nullptr;
  }
  [event retain];
  return gpu->makeResource<MtlSemaphore>(event, value);
}

MtlSemaphore::MtlSemaphore(id<MTLEvent> event, uint64_t value)
    : _event(event), _value(value) {
}

BackendSemaphore MtlSemaphore::getBackendSemaphore() const {
  if (_event == nil) {
    return {};
  }
  MtlSemaphoreInfo mtlInfo = {};
  mtlInfo.event = (__bridge const void*)_event;
  mtlInfo.value = _value;
  return BackendSemaphore(mtlInfo);
}

void MtlSemaphore::onRelease(MtlGPU*) {
  if (_event != nil) {
    [_event release];
    _event = nil;
  }
}

}  // namespace tgfx
