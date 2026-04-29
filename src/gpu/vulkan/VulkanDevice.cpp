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

#include "VulkanDevice.h"
#include "VulkanGPU.h"

namespace tgfx {

std::shared_ptr<VulkanDevice> VulkanDevice::Make() {
  auto gpu = VulkanGPU::Make();
  if (!gpu) {
    return nullptr;
  }
  auto device = std::shared_ptr<VulkanDevice>(new VulkanDevice(std::move(gpu)));
  device->weakThis = device;
  return device;
}

VulkanDevice::VulkanDevice(std::unique_ptr<VulkanGPU> gpu) : Device(std::move(gpu)) {
}

VulkanDevice::~VulkanDevice() {
  static_cast<VulkanGPU*>(_gpu)->releaseAll(true);
}

}  // namespace tgfx
