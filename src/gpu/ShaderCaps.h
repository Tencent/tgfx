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

#include <cstdint>
#include <string>
#include "tgfx/gpu/GPU.h"

namespace tgfx {
/**
 * ShaderCaps describes the capabilities of the shader language.
 */
class ShaderCaps {
 public:
  /**
   * Creates a new ShaderCaps instance based on the provided GPU backend.
   */
  explicit ShaderCaps(GPU* gpu);

  /**
   * Indicates if the shader language requires precision modifiers (lowp, mediump, highp) to be
   * explicitly specified for floating point types.
   */
  bool usesPrecisionModifiers = false;

  /**
   * Indicates if the shader language supports framebuffer fetch, which allows reading the current
   * contents of the framebuffer in the fragment shader.
   */
  bool frameBufferFetchSupport = false;

  /**
   * The extension string required to enable framebuffer fetch support, if any.
   */
  std::string frameBufferFetchExtensionString;

  /**
   * Returns the maximum number of texture samplers that can be used in a shader.
   */
  int maxFragmentSamplers = 0;

  /**
   * Returns the maximum size in bytes of a uniform buffer object (UBO) supported by the
   * shader language.
   */
  int64_t maxUBOSize = 0;

  /**
   * Returns the required alignment in bytes for offsets within a uniform buffer object (UBO).
   */
  int uboOffsetAlignment = 1;

  /**
   * Whether texture sampling must occur in uniform control flow (not inside divergent branches).
   * Required by WebGPU/WGSL; not required by GLSL/Metal.
   */
  bool requiresUniformControlFlow = false;
};
}  // namespace tgfx
