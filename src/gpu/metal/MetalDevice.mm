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

#include "MetalDevice.h"
#include <mutex>
#include <unordered_map>
#include "MetalCommandQueue.h"
#include "MetalGPU.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

static std::mutex deviceMapLocker = {};
static std::unordered_map<id<MTLDevice>, MetalDevice*> deviceMap = {};

std::shared_ptr<MetalDevice> MetalDevice::Make() {
  @autoreleasepool {
    id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
    auto result = MakeFrom(metalDevice);
    [metalDevice release];
    return result;
  }
}

std::shared_ptr<MetalDevice> MetalDevice::MakeFrom(id<MTLDevice> metalDevice) {
  if (metalDevice == nil) {
    return nullptr;
  }
  std::unique_lock<std::mutex> autoLock(deviceMapLocker);
  auto result = deviceMap.find(metalDevice);
  if (result != deviceMap.end()) {
    auto device = result->second->weakThis.lock();
    if (device != nullptr) {
      return std::static_pointer_cast<MetalDevice>(device);
    }
    deviceMap.erase(result);
  }
  autoLock.unlock();
  @autoreleasepool {
    auto gpu = MetalGPU::Make(metalDevice);
    if (!gpu) {
      return nullptr;
    }
    auto device = std::shared_ptr<MetalDevice>(new MetalDevice(std::move(gpu)));
    device->weakThis = device;
    // Re-check under the lock: another thread may have registered a device for the same
    // MTLDevice while we were creating this one. Return the existing one in that case, and
    // let the loser device be destroyed, whose ~MetalDevice() keeps the winner's entry intact.
    autoLock.lock();
    auto registered = deviceMap.find(metalDevice);
    if (registered != deviceMap.end()) {
      auto existing = registered->second->weakThis.lock();
      if (existing != nullptr) {
        // Unlock before returning, so that the losing device created above can acquire the lock
        // in ~MetalDevice() while being destroyed as a local shared_ptr.
        autoLock.unlock();
        return std::static_pointer_cast<MetalDevice>(existing);
      }
      deviceMap.erase(registered);
    }
    deviceMap[metalDevice] = device.get();
    return device;
  }
}

MetalDevice::MetalDevice(std::unique_ptr<MetalGPU> gpu) : Device(std::move(gpu)) {
}

MetalDevice::~MetalDevice() {
  static_cast<MetalGPU*>(_gpu)->releaseAll(true);
  std::lock_guard<std::mutex> autoLock(deviceMapLocker);
  // Only erase the entry if it still points to this device. It may have been taken over by a
  // device that won the registration race in MakeFrom().
  auto result = deviceMap.find(metalDevice());
  if (result != deviceMap.end() && result->second == this) {
    deviceMap.erase(result);
  }
}

id<MTLDevice> MetalDevice::metalDevice() const {
  return static_cast<MetalGPU*>(_gpu)->device();
}

bool MetalDevice::onLockContext() {
  // Metal doesn't require explicit context locking like OpenGL
  // The Metal device and command queues are thread-safe
  return true;
}

void MetalDevice::onUnlockContext() {
  // Metal doesn't require explicit context unlocking
  // No-op for Metal
}

}  // namespace tgfx