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
 * The Metal interface for drawing graphics.
 */
class MetalDevice : public Device {
 public:
  /**
   * Creates a MetalDevice using the system default MTLDevice. If a MetalDevice has already been
   * created for the same MTLDevice and is still alive, the existing one is returned, so that
   * multiple callers share the same GPU caches (command queue, shaders, and resources).
   */
  static std::shared_ptr<MetalDevice> Make();

  /**
   * Creates a MetalDevice from an existing MTLDevice. The device parameter is a pointer to an
   * id<MTLDevice> object. If a MetalDevice has already been created from the same id<MTLDevice>
   * and is still alive, the existing one is returned, so that multiple MetalDevices with the same
   * MTLDevice share the same GPU caches (command queue, shaders, and resources). The caller keeps
   * ownership of the MTLDevice and can release it right after this call returns.
   */
  static std::shared_ptr<MetalDevice> MakeFrom(void* device);

  ~MetalDevice() override;

  /**
   * Returns the underlying MTLDevice as a pointer to an id<MTLDevice> object.
   */
  void* metalDevice() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit MetalDevice(std::unique_ptr<class MetalGPU> gpu);
};

}  // namespace tgfx
