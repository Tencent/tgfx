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

#include "GLFragmentShaderBuilder.h"
#include "GLRenderPipeline.h"
#include "GLVertexShaderBuilder.h"
#include "gpu/PipelineProgram.h"
#include "gpu/ProgramBuilder.h"
#include "gpu/UniformHandler.h"

namespace tgfx {
class GLProgramBuilder : public ProgramBuilder {
 public:
  std::string versionDeclString() override;

  std::string textureFuncName() const override;

  std::string getShaderVarDeclarations(const ShaderVar& var, ShaderStage stage) const override;

  std::string getUniformBlockDeclaration(ShaderStage stage,
                                         const std::vector<Uniform>& uniforms) const override;

  bool isLegacyES() const;

 private:
  GLProgramBuilder(Context* context, const ProgramInfo* programInfo);

  std::unique_ptr<PipelineProgram> finalize();

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
  GLVertexShaderBuilder _vertexBuilder;
  GLFragmentShaderBuilder _fragBuilder;
  size_t vertexStride = 0;

  friend class ProgramBuilder;
};
}  // namespace tgfx
