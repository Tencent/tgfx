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

#include <set>
#include "gpu/ShaderModuleRegistry.h"
#include "gpu/glsl/GLSLProgramBuilder.h"

namespace tgfx {

/**
 * ModularProgramBuilder generates GLSL programs using modular .glsl shader functions for supported
 * FragmentProcessors. It extends GLSLProgramBuilder and overrides only the FP emission step,
 * reusing the existing GP and XP code paths unchanged.
 *
 * Unsupported pipelines (those containing FPs without modular modules) fall through to the legacy
 * GLSLProgramBuilder path via CanUseModularPath() check at the call site.
 */
class ModularProgramBuilder : public GLSLProgramBuilder {
 public:
  ModularProgramBuilder(Context* context, const ProgramInfo* programInfo);

  /**
   * Returns true if ALL fragment processors in the ProgramInfo can be handled by the modular path
   * (including container FPs like ClampedGradientEffect whose children are all modular).
   */
  static bool CanUseModularPath(const ProgramInfo* programInfo);

 protected:
  bool emitAndInstallProcessors() override;

 private:
  /**
   * Replaces the parent's emitAndInstallFragProcessors(). Emits FP code using modular function
   * calls instead of emitCode() string appending.
   */
  void emitModularFragProcessors(std::string* color, std::string* coverage);

  /**
   * Emits modular FS code for a single top-level FP and its children. Returns the output variable
   * name.
   */
  std::string emitModularFragProc(const FragmentProcessor* processor,
                                  size_t transformedCoordVarsIdx, const std::string& input);

  /**
   * Emits code for ClampedGradientEffect using inline control flow + child module function calls.
   */
  void emitClampedGradientEffect(const FragmentProcessor* processor,
                                 size_t transformedCoordVarsIdx, const std::string& input,
                                 const std::string& output);

  /**
   * Emits a call to a leaf modular FP function (e.g., FP_ConstColor).
   */
  void emitLeafFPCall(const FragmentProcessor* processor, size_t transformedCoordVarsIdx,
                      const std::string& input, const std::string& output);

  /**
   * Includes a module's function source into the FS Functions section (once per module).
   */
  void includeModule(ShaderModuleID id);

  /**
   * Emits #define directives for a processor's compile-time switches.
   */
  void emitProcessorDefines(const FragmentProcessor* processor);

  std::set<ShaderModuleID> includedModules;
};

}  // namespace tgfx
