/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/Swizzle.h"
#include "gpu/UniformHandler.h"
#include "gpu/opengl/GLUniformBuffer.h"

namespace tgfx {
static constexpr int UNUSED_UNIFORM = -1;

struct GLUniform {
  ShaderVar variable;
  ShaderFlags visibility = ShaderFlags::None;
  int location = UNUSED_UNIFORM;
};

class GLUniformHandler : public UniformHandler {
 private:
  explicit GLUniformHandler(ProgramBuilder* program) : UniformHandler(program) {
  }

  std::string internalAddUniform(ShaderFlags visibility, SLType type,
                                 const std::string& name) override;

  SamplerHandle internalAddSampler(const TextureSampler* sampler, const std::string& name) override;

  const ShaderVar& samplerVariable(SamplerHandle handle) const override {
    return samplers[handle.toIndex()].variable;
  }

  const Swizzle& samplerSwizzle(SamplerHandle handle) const override {
    return samplerSwizzles[handle.toIndex()];
  }

  std::string getUniformDeclarations(ShaderFlags visibility) const override;

  void resolveUniformLocations(unsigned programID);

  std::unique_ptr<GLUniformBuffer> makeUniformBuffer() const;

  std::vector<GLUniform> uniforms = {};
  std::vector<GLUniform> samplers = {};
  std::vector<Swizzle> samplerSwizzles = {};

  friend class GLProgramBuilder;
};
}  // namespace tgfx
