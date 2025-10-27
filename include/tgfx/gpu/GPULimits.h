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

#include <cinttypes>

namespace tgfx {
/**
 * GPULimits describes the various limits of the GPU.
 */
class GPULimits {
 public:
  /**
   * Returns the maximum 2D texture dimension supported by the GPU.
   */
  int maxTextureDimension2D = 0;

  /**
   * Returns the maximum number of texture samplers available to a single shader stage.
   */
  int maxSamplersPerShaderStage = 0;

  /**
   * Returns the maximum size in bytes of a uniform buffer binding.
   */
  int maxUniformBufferBindingSize = 0;

  /**
   * Returns the minimum required alignment in bytes for uniform buffer offsets.
   */
  int minUniformBufferOffsetAlignment = 0;
};
}  // namespace tgfx
