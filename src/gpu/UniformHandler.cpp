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

std::string UniformHandler::addUniform(const std::string& name, UniformFormat format,
                                       ShaderStage stage) {
  auto uniformName = programBuilder->nameVariable(name);
  switch (stage) {
    case ShaderStage::Vertex:
      vertexUniforms.emplace_back(uniformName, format);
      break;
    case ShaderStage::Fragment:
      fragmentUniforms.emplace_back(uniformName, format);
      break;
  }
  return uniformName;
}

SamplerHandle UniformHandler::addSampler(std::shared_ptr<GPUTexture> texture,
                                         const std::string& name) {
  // The same texture can be added multiple times, each with a different name.
  UniformFormat format;
  switch (texture->type()) {
    case GPUTextureType::External:
      programBuilder->fragmentShaderBuilder()->addFeature(
          PrivateFeature::OESTexture,
          programBuilder->getContext()->caps()->shaderCaps()->oesTextureExtension);
      format = UniformFormat::TextureExternalSampler;
      break;
    case GPUTextureType::Rectangle:
      format = UniformFormat::Texture2DRectSampler;
      break;
    default:
      format = UniformFormat::Texture2DSampler;
      break;
  }
  auto samplerName = programBuilder->nameVariable(name);
  samplers.emplace_back(samplerName, format);
  auto caps = programBuilder->getContext()->caps();
  auto& swizzle = caps->getReadSwizzle(texture->format());
  samplerSwizzles.push_back(swizzle);
  return SamplerHandle(samplers.size() - 1);
}

ShaderVar UniformHandler::getSamplerVariable(SamplerHandle handle) const {
  auto& uniform = samplers[handle.toIndex()];
  return ShaderVar(uniform);
}

std::unique_ptr<UniformData> UniformHandler::makeUniformData(ShaderStage stage) const {
  if (stage == ShaderStage::Vertex && vertexUniforms.empty()) {
    return nullptr;
  }

  if (stage == ShaderStage::Fragment && fragmentUniforms.empty()) {
    return nullptr;
  }

  auto shaderCaps = programBuilder->getContext()->caps()->shaderCaps();
  return std::unique_ptr<UniformData>(new UniformData(
      stage == ShaderStage::Vertex ? vertexUniforms : fragmentUniforms, shaderCaps->uboSupport));
}

std::string UniformHandler::getUniformDeclarations(ShaderStage stage) const {
  std::string ret;
  auto& uniforms = stage == ShaderStage::Vertex ? vertexUniforms : fragmentUniforms;
  auto shaderCaps = programBuilder->getContext()->caps()->shaderCaps();
  if (shaderCaps->uboSupport) {
    ret += programBuilder->getUniformBlockDeclaration(stage, uniforms);
  } else {
    for (auto& uniform : uniforms) {
      ret += programBuilder->getShaderVarDeclarations(ShaderVar(uniform), stage);
      ret += ";\n";
    }
  }

  if (stage == ShaderStage::Fragment) {
    for (const auto& sampler : samplers) {
      ret += programBuilder->getShaderVarDeclarations(ShaderVar(sampler), stage);
      ret += ";\n";
    }
  }
  return ret;
}

}  // namespace tgfx
