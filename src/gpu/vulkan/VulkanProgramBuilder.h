/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/ProgramBuilder.h"
#include "gpu/UniformHandler.h"
#include "gpu/VaryingHandler.h"
#include "gpu/glsl/GLSLFragmentShaderBuilder.h"
#include "gpu/glsl/GLSLVertexShaderBuilder.h"

namespace tgfx {
/**
 * The Vulkan route: the same emitAndInstallProcessors walk as the GLSL builder, with the call
 * sites recorded as structured calls instead of a shader text. finalize() turns the records (and
 * the uniform/sampler declarations the handlers collected) into the linking plan, emits the body
 * module and links it with the operators' precompiled modules.
 */
class VulkanProgramBuilder : public ProgramBuilder {
 public:
  std::string getShaderVarDeclarations(const ShaderVar& var, ShaderStage stage) const override;

  std::string getUniformBlockDeclaration(ShaderStage stage,
                                         const std::vector<Uniform>& uniforms) const override;

 private:
  VulkanProgramBuilder(Context* context, const ProgramInfo* programInfo);

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
  GLSLVertexShaderBuilder _vertexBuilder;
  GLSLFragmentShaderBuilder _fragBuilder;
  /// The structured calls the emit sites made, in emission order.
  std::vector<CallRecord> fragmentCalls = {};
  /// The structured vertex writes the geometry's emit made, in emission order.
  std::vector<VertexCallRecord> vertexCalls = {};

  friend class ProgramBuilder;
};
}  // namespace tgfx
