/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "FragmentShaderBuilder.h"
#include "ProgramInfo.h"
#include "UniformHandler.h"
#include "VaryingHandler.h"
#include "VertexShaderBuilder.h"
#include "gpu/Uniform.h"
#include "gpu/processors/GeometryProcessor.h"
#include "tgfx/gpu/Attribute.h"

namespace tgfx {
/**
 * Structured description of a shader input/output entry captured at finalize time. The `name` is
 * the exact identifier used in the generated shader text (already mangled); `format` is carried
 * through as the raw enum value so offline tools can map it back to GLSL/HLSL types without
 * needing access to the full tgfx type system.
 */
struct CaptureUniform {
  std::string name;
  int format = 0;  // UniformFormat enum value
};

struct CaptureAttribute {
  std::string name;
  int format = 0;             // VertexFormat enum value
  bool instanceStep = false;  // true for instance-rate attributes, false for vertex-rate
};

/**
 * Optional output capture for ProgramBuilder::CreateProgram. When a caller passes a non-null
 * pointer, the builder fills:
 *   - the final VS/FS shader source text (as handed to the GPU driver);
 *   - the uniform / sampler / attribute layout the shader text depends on.
 * The capture is purely observational — it does not affect program creation.
 */
struct ShaderTextCapture {
  std::string vertexShader;
  std::string fragmentShader;
  std::vector<CaptureUniform> vertexUniforms;
  std::vector<CaptureUniform> fragmentUniforms;
  std::vector<CaptureUniform> samplers;
  std::vector<CaptureAttribute> attributes;
};

class ProgramBuilder {
 public:
  /**
   * Generates a shader program.
   */
  static std::shared_ptr<Program> CreateProgram(Context* context, const ProgramInfo* programInfo,
                                                ShaderTextCapture* capture = nullptr);

  virtual ~ProgramBuilder() = default;

  Context* getContext() const {
    return context;
  }

  const ProgramInfo* getProgramInfo() const {
    return programInfo;
  }

  virtual std::string getShaderVarDeclarations(const ShaderVar& var, ShaderStage stage) const = 0;

  virtual std::string getUniformBlockDeclaration(ShaderStage stage,
                                                 const std::vector<Uniform>& uniforms) const = 0;

  /**
   * Generates a name for a variable. The generated string will be mangled to be processor-specific.
   */
  std::string nameVariable(const std::string& name) const;

  virtual UniformHandler* uniformHandler() = 0;

  virtual const UniformHandler* uniformHandler() const = 0;

  virtual VaryingHandler* varyingHandler() = 0;

  virtual VertexShaderBuilder* vertexShaderBuilder() = 0;

  virtual FragmentShaderBuilder* fragmentShaderBuilder() = 0;

  virtual bool emitAndInstallProcessors() = 0;

  void finalizeShaders();

  // ---- Offline-replayable text builders ----
  //
  // Pure functions that encapsulate the text-construction rules for main()-body boilerplate
  // shared by the legacy and modular program builders. Stateless so that offline shader variant
  // tools can call them directly to replay the exact bytes produced at runtime.

  /** `"<name> = TGFX_OutputSwizzle(<name>);"` — final output swizzle call. */
  static std::string BuildOutputSwizzleCall(const std::string& outputName);

  /** `"vec4 <name>;"` — bare vec4 declaration used by nameExpression(). */
  static std::string BuildVec4Decl(const std::string& name);

 protected:
  Context* context = nullptr;
  const ProgramInfo* programInfo = nullptr;
  int numFragmentSamplers = 0;

  ProgramBuilder(Context* context, const ProgramInfo* programInfo);

  virtual bool checkSamplerCounts() = 0;

  virtual void emitAndInstallGeoProc(std::string* outputColor, std::string* outputCoverage) = 0;

  virtual void emitAndInstallXferProc(const std::string& colorIn,
                                      const std::string& coverageIn) = 0;

  SamplerHandle emitSampler(std::shared_ptr<Texture> texture, const std::string& name);

  void emitFSOutputSwizzle();

 private:
  std::vector<const Processor*> currentProcessors = {};
  std::vector<ShaderVar> transformedCoordVars = {};
  std::string subsetVarName = {};

  /**
   * Generates a possibly mangled name for a stage variable and writes it to the fragment shader.
   */
  void nameExpression(std::string* output, const std::string& baseName);

  friend class FragmentShaderBuilder;
  friend class ModularProgramBuilder;
};
}  // namespace tgfx
