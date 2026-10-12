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
#include <unordered_map>
#include "core/utils/Log.h"
#include "core/utils/SingleOwner.h"
#include "core/utils/UniqueID.h"
#include "gpu/DeviceRegistry.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

namespace {
struct RegistryEntry {
  Device* device = nullptr;
  std::weak_ptr<Device> weakDevice = {};
};

std::mutex registryLocker = {};
std::unordered_map<DeviceKey, RegistryEntry, DeviceKeyHash> deviceRegistry = {};
}  // namespace

Device::Device(std::unique_ptr<GPU> gpu) : _gpu(gpu.release()), _uniqueID(UniqueID::Next()) {
  DEBUG_ASSERT(_gpu != nullptr);
}

Device::~Device() {
  delete context;
  delete _gpu;
  std::lock_guard<std::mutex> autoLock(registryLocker);
  // The weak_ptr of this device has already expired, so identify the entry by the raw pointer.
  // The number of live devices is single-digit, so a linear scan is fine.
  for (auto it = deviceRegistry.begin(); it != deviceRegistry.end(); ++it) {
    if (it->second.device == this) {
      deviceRegistry.erase(it);
      break;
    }
  }
}

std::shared_ptr<Device> Device::RegisterNative(const std::shared_ptr<Device>& device,
                                               const DeviceKey& key) {
  if (device == nullptr) {
    return nullptr;
  }
  std::lock_guard<std::mutex> autoLock(registryLocker);
  auto result = deviceRegistry.find(key);
  if (result != deviceRegistry.end()) {
    auto existing = result->second.weakDevice.lock();
    if (existing != nullptr) {
      return existing;
    }
    deviceRegistry.erase(result);
  }
  deviceRegistry[key] = {device.get(), device};
  return device;
}

std::shared_ptr<Device> Device::FindNative(const DeviceKey& key) {
  std::lock_guard<std::mutex> autoLock(registryLocker);
  auto result = deviceRegistry.find(key);
  if (result != deviceRegistry.end()) {
    auto device = result->second.weakDevice.lock();
    if (device != nullptr) {
      return device;
    }
    deviceRegistry.erase(result);
  }
  return nullptr;
}

std::vector<std::shared_ptr<Device>> Device::GetAllNative() {
  std::lock_guard<std::mutex> autoLock(registryLocker);
  std::vector<std::shared_ptr<Device>> devices = {};
  for (auto it = deviceRegistry.begin(); it != deviceRegistry.end();) {
    auto device = it->second.weakDevice.lock();
    if (device != nullptr) {
      devices.push_back(std::move(device));
      ++it;
    } else {
      it = deviceRegistry.erase(it);
    }
  }
  return devices;
}

Context* Device::lockContext() {
  locker.lock();
  if (_contextLost) {
    locker.unlock();
    return nullptr;
  }
  contextLocked = onLockContext();
  if (!contextLocked) {
    locker.unlock();
    return nullptr;
  }
  if (context == nullptr) {
    context = new Context(this, _gpu);
  }
  SINGLE_OWNER_ACQUIRE(*context->singleOwner);
  return context;
}

void Device::unlock() {
  if (contextLocked) {
    SINGLE_OWNER_RELEASE(*context->singleOwner);
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
