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

#import <Metal/Metal.h>
#include "tgfx/gpu/Device.h"

namespace tgfx {

/**
 * The Metal interface for drawing graphics.
 */
class MetalDevice : public Device {
 public:
  /**
   * Creates a MetalDevice using the system default MTLDevice. MTLCreateSystemDefaultDevice()
   * returns the system-wide singleton, so calling this method repeatedly yields the same
   * MetalDevice as long as it is still alive. Do not assume a brand-new independent device is
   * created.
   */
  static std::shared_ptr<MetalDevice> Make();

  /**
   * Creates a MetalDevice from an existing MTLDevice.
   * @param device The MTLDevice to create the MetalDevice from. Must not be nil.
   * @return The existing live MetalDevice if one has already been created from the same
   * MTLDevice, so that all callers share the same GPU caches (command queue, shaders, and
   * resources), otherwise a newly created one. Returns nullptr if device is nil or the GPU fails
   * to initialize. The caller keeps ownership of the MTLDevice and can release it right after
   * this call returns. Note that reusing an existing Device for the same native device is
   * currently a Metal-only semantic: the Vulkan, D3D12, and WebGPU backends still create a new
   * Device on every call.
   */
  static std::shared_ptr<MetalDevice> MakeFrom(id<MTLDevice> device);

  ~MetalDevice() override;

  /**
   * Returns the underlying MTLDevice.
   */
  id<MTLDevice> metalDevice() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit MetalDevice(std::unique_ptr<class MetalGPU> gpu);
};

}  // namespace tgfx
