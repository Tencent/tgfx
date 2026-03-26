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
 * The D3D12 interface for drawing graphics.
 */
class D3D12Device : public Device {
 public:
  /**
   * Creates a new D3D12Device using the default hardware adapter. Returns nullptr if D3D12 is not
   * available.
   */
  static std::shared_ptr<D3D12Device> Make();

  /**
   * Creates a new D3D12Device from an existing ID3D12Device. The device parameter is a pointer to
   * an ID3D12Device object. Returns nullptr if the device is invalid.
   */
  static std::shared_ptr<D3D12Device> MakeFrom(void* device);

  ~D3D12Device() override;

  /**
   * Returns the underlying ID3D12Device as a raw pointer.
   */
  void* d3d12Device() const;

 protected:
  bool onLockContext() override;
  void onUnlockContext() override;

 private:
  explicit D3D12Device(std::unique_ptr<class D3D12GPU> gpu);
};

}  // namespace tgfx
