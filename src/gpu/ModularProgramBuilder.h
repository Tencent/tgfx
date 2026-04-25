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

#include <functional>
#include <set>
#include <vector>
#include "gpu/MangledResources.h"
#include "gpu/SamplerHandle.h"
#include "gpu/ShaderCallManifest.h"
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
  using CoordTransformFunc = std::function<std::string(const std::string&)>;

  std::string emitModularFragProc(const FragmentProcessor* processor,
                                  size_t transformedCoordVarsIdx, const std::string& input,
                                  CoordTransformFunc coordTransformFunc = {},
                                  bool skipSamplerCollection = false, size_t samplerOffset = 0);

  /**
   * Emits a container FP using its buildContainerCallStatement() method. Recursively emits child
   * FPs first, then calls the container's .glsl function with child outputs as arguments.
   * Returns true if the container was handled, false to fall through to legacy dispatch.
   */
  bool emitModularContainerFP(const FragmentProcessor* processor, size_t transformedCoordVarsIdx,
                              const std::string& input, const std::string& output,
                              size_t samplerOffset);

  /**
   * Emits a call to a leaf modular FP function via buildCallStatement().
   */
  void emitLeafFPCall(const FragmentProcessor* processor, size_t transformedCoordVarsIdx,
                      const std::string& input, const std::string& output,
                      const CoordTransformFunc& coordTransformFunc = {}, size_t samplerOffset = 0);

  /**
   * Includes a module's function source into the FS Functions section (once per module).
   */
  void includeModule(ShaderModuleID id);

  /**
   * Includes a module's function source into the VS Functions section (once per module).
   * Used for GeometryProcessor vertex shader modules.
   */
  void includeVSModule(ShaderModuleID id);

  /**
   * Emits #define directives for a processor's compile-time switches.
   */
  void emitProcessorDefines(const FragmentProcessor* processor);

  /**
   * Returns the name of a 2D coordinate variable ready for texture sampling. If the coord
   * varying is already vec2, returns its name unchanged. If vec3 (perspective), emits a call to
   * TGFX_PerspDivide() (from tgfx_fs_boilerplate.glsl) and returns the resulting 2D name.
   */
  std::string emitPerspCoordDivide(const ShaderVar& coordVar);

  /**
   * Renders a ShaderCallManifest into a GLSL statement string. Supports both function-call mode
   * (functionName non-empty) and raw-statement mode (functionName empty, statement used verbatim).
   *
   * Function-call mode produces either:
   *   `outputType outputVarName = functionName(arg0, arg1, ...);`
   *   (when declareOutput == true)
   * or:
   *   `functionName(arg0, arg1, ..., outputVarName);`
   *   (when declareOutput == false — used by XPs with out-parameter signatures).
   *
   * The logic is deterministic string templating with no algorithmic branches, so UE's offline
   * shader variant enumerator can replicate it exactly.
   */
  static std::string RenderManifest(const ShaderCallManifest& manifest);

  // ---- Offline-replayable text builders ----
  //
  // The following pure functions are the canonical text-construction rules for the main()-body
  // boilerplate emitted by the modular builder. They are stateless (static, take only string /
  // int arguments) so that offline shader variant tools can call them directly to replay the
  // exact bytes produced at runtime, without needing to run the C++ orchestration control flow.
  //
  // Every call site inside ModularProgramBuilder that would otherwise format a string via
  // codeAppendf() should route through one of these functions — see the replacement audit in
  // plans/stellar-cascade-lovelace.md.

  /** `"{ // Processor<idx> : <name>\n"` — FS processor block opener. */
  static std::string BuildProcessorHeader(int processorIndex, const std::string& processorName);

  /** `"}"` — FS processor block closer. Kept as a function so call sites are uniform. */
  static std::string BuildProcessorFooter();

  /** `"// Processor<idx> : <name>\n"` — VS processor separator comment. */
  static std::string BuildVSProcessorComment(int processorIndex, const std::string& processorName);

  /** `"<type> <name> = <rhs>;"` — single-line local variable declaration with initializer. */
  static std::string BuildTempVar(const std::string& type, const std::string& name,
                                  const std::string& rhs);

  /** `"<lhs> = <rhs>;\n"` — plain assignment bridging processor output to section output. */
  static std::string BuildAssignment(const std::string& lhs, const std::string& rhs);

  /** `"highp vec2 <dst> = TGFX_PerspDivide(<src>);"` — perspective divide call emission. */
  static std::string BuildPerspDivideDecl(const std::string& dstName, const std::string& srcName);

  // ---- GP/XP override methods ----

  void emitAndInstallGeoProc(std::string* outputColor, std::string* outputCoverage) override;
  void emitAndInstallXferProc(const std::string& colorIn, const std::string& coverageIn) override;

  struct FPResources {
    MangledUniforms uniforms;
    MangledVaryings varyings;
    MangledSamplers samplers;
  };

  std::set<ShaderModuleID> includedModules;
  std::set<ShaderModuleID> includedVSModules;
  // Sampler handles collected during emitModularFragProc, available for emitLeafFPCall.
  std::vector<SamplerHandle> currentTexSamplers;
};

}  // namespace tgfx
