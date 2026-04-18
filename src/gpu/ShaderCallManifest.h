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
 * Result of a Processor's build*CallStatement() — a structured description of the GLSL call to
 * emit. Two rendering modes are supported:
 *
 *   1. Function-call mode (FP/XP buildCallStatement): when functionName is non-empty, the renderer
 *      produces `[outputType] outputVarName = functionName(arg0, arg1, ...);` or, if
 *      declareOutput is false, `functionName(arg0, arg1, ..., outputVarName);` (e.g. XP using an
 *      out-parameter).
 *
 *   2. Raw-statement mode (GP buildColorCallExpr / buildCoverageCallExpr / buildVSCallExpr): when
 *      functionName is empty, the renderer emits `statement` verbatim. Used for expression-shaped
 *      outputs like `vec4 gpColor = uniformName;` that are not simple function calls.
 *
 * Both modes share:
 *   - outputVarName: the variable name produced (used as input to the next stage).
 *   - preamble: additional #define directives to place in the Definitions section.
 *   - includeFiles: .glsl module paths (without extension) that must be injected.
 */
struct ShaderCallManifest {
  /**
   * Function name to invoke in function-call mode. Empty for raw-statement mode.
   * Example: "TGFX_TextureEffect".
   */
  std::string functionName;

  /**
   * Argument expressions passed to functionName (in order). Each expression is already
   * mangled / evaluated; the renderer concatenates them with ", ".
   * Example: ["color_fp0", "coord_0", "texSampler_P2"].
   */
  std::vector<std::string> argExpressions;

  /**
   * Output variable name produced by this call. Used as input for the next Processor.
   * Example: "color_fp1".
   */
  std::string outputVarName;

  /**
   * GLSL type of outputVarName. Used only in function-call mode when declareOutput is true.
   * Defaults to "vec4" which covers all FP chains.
   */
  std::string outputType = "vec4";

  /**
   * If true (the default), the renderer emits `outputType outputVarName = functionName(args);`.
   * If false, the renderer emits `functionName(args, outputVarName);` — used by XPs whose shader
   * function writes to an out-parameter instead of returning a value.
   */
  bool declareOutput = true;

  /**
   * Raw GLSL statement used in raw-statement mode (when functionName is empty). The renderer
   * appends this verbatim.
   */
  std::string statement;

  /**
   * GLSL preprocessor directives that must appear in the Definitions section, outside any
   * function body. Used by container FPs (e.g. GaussianBlur1D) to inject sampling macros whose
   * bodies depend on mangled resource names only known at emit time.
   */
  std::string preamble;

  /**
   * Shader function files required by this Processor (including children for containers).
   * These are relative paths without extension, e.g. "fragment/texture_effect.frag".
   */
  std::vector<std::string> includeFiles;
};

}  // namespace tgfx
