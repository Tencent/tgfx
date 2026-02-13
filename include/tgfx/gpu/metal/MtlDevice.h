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
 * Metal device for GPU rendering.
 */
class MtlDevice : public Device {
 public:
  /**
   * Creates a Metal device with the default Metal device.
   */
  static std::shared_ptr<MtlDevice> Make();

  /**
   * Creates a Metal device with the specified Metal device. The device parameter is a pointer to
   * an id<MTLDevice> object.
   */
  static std::shared_ptr<MtlDevice> MakeFrom(void* device);

  ~MtlDevice() override;

  /**
   * Returns the Metal device as a pointer to an id<MTLDevice> object.
   */
  void* mtlDevice() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit MtlDevice(std::unique_ptr<class MtlGPU> gpu);

  void* device = nullptr;
};

}  // namespace tgfx