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
#include "MetalGPU.h"
#include "MetalCommandQueue.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

std::shared_ptr<MetalDevice> MetalDevice::Make() {
  @autoreleasepool {
    id<MTLDevice> metalDevice = MTLCreateSystemDefaultDevice();
    auto result = MakeFrom(metalDevice);
    [metalDevice release];
    return result;
  }
}

std::shared_ptr<MetalDevice> MetalDevice::MakeFrom(void* metalDevice) {
  if (!metalDevice) {
    return nullptr;
  }
  @autoreleasepool {
    auto gpu = MetalGPU::Make((id<MTLDevice>)metalDevice);
    if (!gpu) {
      return nullptr;
    }
    auto device = std::shared_ptr<MetalDevice>(new MetalDevice(std::move(gpu)));
    device->weakThis = device;
    return device;
  }
}

MetalDevice::MetalDevice(std::unique_ptr<MetalGPU> gpu) : Device(std::move(gpu)) {
}

MetalDevice::~MetalDevice() {
  static_cast<MetalGPU*>(_gpu)->releaseAll(true);
}

void* MetalDevice::metalDevice() const {
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