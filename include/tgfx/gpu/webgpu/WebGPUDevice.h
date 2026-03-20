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

#include "tgfx/gpu/Device.h"

namespace tgfx {

/**
 * The WebGPU interface for drawing graphics.
 */
class WebGPUDevice : public Device {
 public:
  /**
   * Creates a new WebGPUDevice by requesting the default adapter and device.
   */
  static std::shared_ptr<WebGPUDevice> Make();

  /**
   * Creates a new WebGPUDevice from an existing WGPUDevice. The device parameter is a pointer to a
   * WGPUDevice object.
   */
  static std::shared_ptr<WebGPUDevice> MakeFrom(void* device);

  ~WebGPUDevice() override;

  /**
   * Returns the underlying WGPUDevice as a void pointer.
   */
  void* webgpuDevice() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit WebGPUDevice(std::unique_ptr<class WebGPUGPU> gpu);
};

}  // namespace tgfx
