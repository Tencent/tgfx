////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/Program.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/UniformHandler.h"
#include "gpu/VaryingHandler.h"
#include "gpu/glsl/GLSLFragmentShaderBuilder.h"
#include "gpu/glsl/GLSLVertexShaderBuilder.h"
#include "gpu/ir/ProgramDescription.h"

namespace tgfx {
/**
 * The WebGPU route: the same emitAndInstallProcessors walk as the GLSL builder, with the records
 * printed as WGSL instead of GLSL. finalize() turns the records (and the uniform/sampler
 * declarations the handlers collected) into the WGSL modules and the render pipeline the browser
 * compiles.
 */
class WebGPUProgramBuilder : public ProgramBuilder {
 public:
  /**
   * Builds one draw's program through the WebGPU runtime route: the processor tree's recorded
   * calls become WGSL text compiled by the browser. Returns nullptr when the draw is outside the
   * route's slice.
   */
  static std::shared_ptr<Program> Create(Context* context, const ProgramInfo* programInfo);

  std::string getShaderVarDeclarations(const ShaderVar& var, ShaderStage stage) const override;

  std::string getUniformBlockDeclaration(ShaderStage stage,
                                         const std::vector<Uniform>& uniforms) const override;

  std::string getUniformBlockDeclaration(ShaderStage stage,
                                         const std::vector<UniformDesc>& uniforms) const override;

  const InterfaceDesc* programInterface() const override {
    return &description.interface;
  }

 private:
  WebGPUProgramBuilder(Context* context, const ProgramInfo* programInfo);

  std::shared_ptr<Program> finalize();

  UniformHandler* uniformHandler() override {
    return &_uniformHandler;
  }

  const UniformHandler* uniformHandler() const override {
    return &_uniformHandler;
  }

  VaryingHandler* varyingHandler() override {
    return &_varyingHandler;
  }

  VertexShaderBuilder* vertexShaderBuilder() override {
    return &_vertexBuilder;
  }

  FragmentShaderBuilder* fragmentShaderBuilder() override {
    return &_fragBuilder;
  }

  bool checkSamplerCounts() override;

  VaryingHandler _varyingHandler;
  UniformHandler _uniformHandler;
  ProgramDescription description = {};
  GLSLVertexShaderBuilder _vertexBuilder;
  GLSLFragmentShaderBuilder _fragBuilder;

  friend class ProgramBuilder;
};
}  // namespace tgfx
