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

#include "MtlDevice.h"
#include "MtlGPU.h"
#include "MtlCommandQueue.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

std::shared_ptr<MtlDevice> MtlDevice::Make() {
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  return MakeFrom(device);
}

std::shared_ptr<MtlDevice> MtlDevice::MakeFrom(id<MTLDevice> mtlDevice) {
  if (!mtlDevice) {
    return nullptr;
  }
  
  auto gpu = MtlGPU::Make(mtlDevice);
  if (!gpu) {
    return nullptr;
  }
  
  auto device = std::shared_ptr<MtlDevice>(new MtlDevice(std::move(gpu)));
  device->weakThis = device;
  return device;
}

MtlDevice::MtlDevice(std::unique_ptr<MtlGPU> gpu) : Device(std::move(gpu)) {
  device = static_cast<MtlGPU*>(_gpu)->device();
}

MtlDevice::~MtlDevice() {
  static_cast<MtlGPU*>(_gpu)->releaseAll(true);
}

id<MTLDevice> MtlDevice::mtlDevice() const {
  return device;
}

bool MtlDevice::onLockContext() {
  // Metal doesn't require explicit context locking like OpenGL
  // The Metal device and command queues are thread-safe
  return true;
}

void MtlDevice::onUnlockContext() {
  // Metal doesn't require explicit context unlocking
  // No-op for Metal
}

}  // namespace tgfx