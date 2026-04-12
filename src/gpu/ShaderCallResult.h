/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include <vector>

namespace tgfx {

/**
 * Result of a Processor's buildCallStatement() — contains the GLSL statement and output variable
 * name for chaining in the main() function body.
 */
struct ShaderCallResult {
  /**
   * Complete GLSL statement including variable declaration and function call.
   * Example: "vec4 color_fp1 = TGFX_TextureEffect(color_fp0, coord, texSampler);"
   */
  std::string statement;

  /**
   * The output variable name produced by this statement, used as input for the next Processor.
   * Example: "color_fp1"
   */
  std::string outputVarName;

  /**
   * GLSL preprocessor directives (e.g. #define macros) that must appear in the Definitions section,
   * outside of any function body. Used by container FPs like GaussianBlur1D to inject child FP
   * sampling macros that depend on mangled resource names only known at emit time.
   */
  std::string preamble;

  /**
   * Shader function files required by this Processor (including children for containers).
   * These are relative paths without extension, e.g. "fragment/texture_effect.frag"
   */
  std::vector<std::string> includeFiles;
};

}  // namespace tgfx
