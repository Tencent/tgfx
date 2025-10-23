/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

namespace tgfx {
/**
 * GPUFeatures describes the various optional features supported by the GPU.
 */
class GPUFeatures {
 public:
  /**
   * Indicates if the GPU supports semaphore synchronization primitives.
   */
  bool semaphore = false;

  /**
   * Indicates whether the GPU supports the CLAMP_TO_BORDER wrap mode for texture coordinates.
   */
  bool clampToBorder = false;

  /**
   * Indicates whether the GPU supports texture barriers. If true, texture writes are
   * immediately visible to subsequent texture reads without needing to flush the pipeline.
   */
  bool textureBarrier = false;
};
}  // namespace tgfx
