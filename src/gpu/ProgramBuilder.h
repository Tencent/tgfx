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
#include "gpu/processors/GeometryProcessor.h"

namespace tgfx {
class ProgramBuilder {
 public:
  /**
   * Generates a shader program.
   */
  static std::shared_ptr<Program> CreateProgram(Context* context, const ProgramInfo* programInfo);

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

 protected:
  Context* context = nullptr;
  const ProgramInfo* programInfo = nullptr;
  int numFragmentSamplers = 0;

  ProgramBuilder(Context* context, const ProgramInfo* programInfo);

  bool emitAndInstallProcessors();

  void finalizeShaders();

  virtual bool checkSamplerCounts() = 0;

 private:
  std::vector<const Processor*> currentProcessors = {};
  std::vector<ShaderVar> transformedCoordVars = {};
  std::string subsetVarName = {};

  /**
   * Generates a possibly mangled name for a stage variable and writes it to the fragment shader.
   */
  void nameExpression(std::string* output, const std::string& baseName);

  void emitAndInstallGeoProc(std::string* outputColor, std::string* outputCoverage);

  void emitAndInstallFragProcessors(std::string* color, std::string* coverage);

  std::string emitAndInstallFragProc(const FragmentProcessor* processor,
                                     size_t transformedCoordVarsIdx, const std::string& input);

  void emitAndInstallXferProc(const std::string& colorIn, const std::string& coverageIn);

  SamplerHandle emitSampler(std::shared_ptr<Texture> texture, const std::string& name);

  void emitFSOutputSwizzle();

  friend class FragmentShaderBuilder;
  friend class ProcessorGuard;
};
}  // namespace tgfx
