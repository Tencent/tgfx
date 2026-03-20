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

namespace tgfx {
/**
 * Types for interacting with WebGPU textures created externally to TGFX.
 */
struct WebGPUTextureInfo {
  /**
   * Pointer to a WGPUTexture object.
   */
  const void* texture = nullptr;

  /**
   * Pointer to a WGPUTextureView object. If nullptr, a default view will be created.
   */
  const void* textureView = nullptr;

  /**
   * The pixel format of this texture (WGPUTextureFormat value).
   */
  unsigned format = 0x12;  // WGPUTextureFormat_RGBA8Unorm
};

/**
 * Types for interacting with WebGPU sync objects. WebGPU has no cross-queue synchronization, so
 * this struct is empty.
 */
struct WebGPUSyncInfo {};
}  // namespace tgfx
