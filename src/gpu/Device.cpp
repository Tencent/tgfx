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

#include "tgfx/gpu/Device.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
Device::Device(std::unique_ptr<GPU> gpu) : _gpu(gpu.release()), _uniqueID(UniqueID::Next()) {
  DEBUG_ASSERT(_gpu != nullptr);
}

Device::~Device() {
  delete context;
  delete _gpu;
}

Context* Device::lockContext() {
  locker.lock();
  contextLocked = onLockContext();
  if (!contextLocked) {
    locker.unlock();
    return nullptr;
  }
  if (context == nullptr) {
    context = new Context(this, _gpu);
  }
  return context;
}

void Device::unlock() {
  if (contextLocked) {
    contextLocked = false;
    onUnlockContext();
  }
  locker.unlock();
}

bool Device::onLockContext() {
  return true;
}

void Device::onUnlockContext() {
}

}  // namespace tgfx
