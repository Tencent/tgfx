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

#include <cstdint>
#include <type_traits>

namespace tgfx {
/**
 * Types for interacting with D3D12 textures created externally to TGFX.
 */
struct D3D12TextureInfo {
  /**
   * Pointer to an ID3D12Resource object representing a texture.
   */
  const void* resource = nullptr;

  /**
   * The pixel format of this texture (DXGI_FORMAT value).
   */
  unsigned format = 0;  // DXGI_FORMAT_UNKNOWN
};

/**
 * Types for interacting with D3D12 synchronization objects created externally to TGFX.
 */
struct D3D12SyncInfo {
  /**
   * Pointer to an ID3D12Fence object.
   */
  const void* fence = nullptr;

  /**
   * The signal value for the fence.
   */
  uint64_t value = 0;
};

static_assert(std::is_trivially_copyable_v<D3D12TextureInfo>);
static_assert(std::is_trivially_copyable_v<D3D12SyncInfo>);
static_assert(std::is_standard_layout_v<D3D12TextureInfo>);
static_assert(std::is_standard_layout_v<D3D12SyncInfo>);

}  // namespace tgfx
