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

#include "gpu/SamplerHandle.h"
#include "gpu/ShaderStage.h"
#include "gpu/ShaderVar.h"
#include "gpu/Swizzle.h"
#include "gpu/Texture.h"
#include "gpu/UniformData.h"

namespace tgfx {
class ProgramBuilder;

class UniformHandler {
 public:
  explicit UniformHandler(ProgramBuilder* builder) : programBuilder(builder) {
  }

  /**
   * Adds a uniform variable to the current program, that has visibility in one or more shaders.
   * visibility is a bitfield of ShaderFlag values indicating from which shaders the uniform should
   * be accessible. At least one bit must be set. The actual uniform name will be mangled. Returns
   * the final uniform name.
   */
  std::string addUniform(const std::string& name, UniformFormat format, ShaderStage stage);

  /**
   * Returns all samplers added by addSampler().
   */
  const std::vector<Uniform>& getSamplers() const {
    return samplers;
  }

  /**
   * Adds a sampler to the current program.
   */
  SamplerHandle addSampler(std::shared_ptr<Texture> texture, const std::string& name);

  /**
   * Returns the sampler variable for the given sampler handle.
   */
  ShaderVar getSamplerVariable(SamplerHandle samplerHandle) const;

  /**
   * Returns the sampler swizzle for the given sampler handle.
   */
  Swizzle getSamplerSwizzle(SamplerHandle samplerHandle) const {
    return samplerSwizzles[samplerHandle.toIndex()];
  }

  std::unique_ptr<UniformData> makeUniformData(ShaderStage stage) const;

  /**
   * Returns the declarations of all uniforms that are visible in the given shader visibility.
   */
  std::string getUniformDeclarations(ShaderStage stage) const;

 private:
  // This is not owned by the class
  ProgramBuilder* programBuilder = nullptr;
  std::vector<Uniform> vertexUniforms = {};
  std::vector<Uniform> fragmentUniforms = {};
  std::vector<Uniform> samplers = {};
  std::vector<Swizzle> samplerSwizzles = {};
};
}  // namespace tgfx
