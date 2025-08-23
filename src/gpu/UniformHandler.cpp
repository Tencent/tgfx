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

#include "UniformHandler.h"
#include "gpu/FragmentShaderBuilder.h"
#include "gpu/ProgramBuilder.h"

namespace tgfx {
static const std::string OES_TEXTURE_EXTENSION = "GL_OES_EGL_image_external";

std::string UniformHandler::addUniform(ShaderFlags visibility, SLType type,
                                       const std::string& name) {
  auto uniformName = programBuilder->nameVariable(name);
  Uniform uniform(uniformName, type, visibility);
  uniforms.push_back(uniform);
  return uniform.name();
}

SamplerHandle UniformHandler::addSampler(GPUTexture* texture, const std::string& name) {
  // The same texture can be added multiple times, each with a different name.
  SLType type;
  switch (texture->type()) {
    case GPUTextureType::External:
      programBuilder->fragmentShaderBuilder()->addFeature(PrivateFeature::OESTexture,
                                                          OES_TEXTURE_EXTENSION);
      type = SLType::TextureExternalSampler;
      break;
    case GPUTextureType::Rectangle:
      type = SLType::Texture2DRectSampler;
      break;
    default:
      type = SLType::Texture2DSampler;
      break;
  }
  auto samplerName = programBuilder->nameVariable(name);
  Uniform uniform = {samplerName, type, ShaderFlags::Fragment};
  samplers.push_back(uniform);
  auto caps = programBuilder->getContext()->caps();
  auto& swizzle = caps->getReadSwizzle(texture->format());
  samplerSwizzles.push_back(swizzle);
  return SamplerHandle(samplers.size() - 1);
}

ShaderVar UniformHandler::getSamplerVariable(SamplerHandle handle) const {
  auto& uniform = samplers[handle.toIndex()];
  return {uniform.name(), uniform.type(), ShaderVar::TypeModifier::Uniform};
}

std::string UniformHandler::getUniformDeclarations(ShaderFlags visibility) const {
  std::string ret;
  for (auto& uniform : uniforms) {
    if ((uniform.visibility() & visibility) == visibility) {
      ShaderVar variable = {uniform.name(), uniform.type(), ShaderVar::TypeModifier::Uniform};
      ret += programBuilder->getShaderVarDeclarations(variable, visibility);
      ret += ";\n";
    }
  }
  for (const auto& sampler : samplers) {
    if ((sampler.visibility() & visibility) == visibility) {
      ShaderVar variable = {sampler.name(), sampler.type(), ShaderVar::TypeModifier::Uniform};
      ret += programBuilder->getShaderVarDeclarations(variable, visibility);
      ret += ";\n";
    }
  }
  return ret;
}

}  // namespace tgfx