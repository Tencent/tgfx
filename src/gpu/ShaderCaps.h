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

#include <string>
#include "gpu/GPU.h"

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
   * The version declaration string to be placed at the top of the shader code. For example,
   * "#version 300 es" for OpenGL ES 3.0, or "#version 150" for OpenGL 3.2.
   */
  std::string versionDeclString;

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
   * Indicates if the framebuffer fetch requires a custom output variable to be declared in the
   * fragment shader. This is true for modern GLSL versions where "inout vec4 <name>" is used
   * instead of the legacy "gl_LastFragData".
   */
  bool frameBufferFetchNeedsCustomOutput = false;

  /**
   * The name of the variable that holds the input color when using framebuffer fetch. This is
   * typically "gl_LastFragData" in legacy OpenGL ES, and "inout vec4 <name>" in modern GLSL.
   */
  std::string frameBufferFetchColorName;

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
  int maxUBOSize = 0;

  /**
   * Returns the required alignment in bytes for offsets within a uniform buffer object (UBO).
   */
  int uboOffsetAlignment = 1;
};
}  // namespace tgfx
