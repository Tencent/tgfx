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

#include "GLUniformHandler.h"
#include "GLProgramBuilder.h"

namespace tgfx {
std::string GLUniformHandler::internalAddUniform(ShaderFlags visibility, SLType type,
                                                 const std::string& name) {
  GLUniform uniform;
  uniform.variable.setType(type);
  uniform.variable.setTypeModifier(ShaderVar::TypeModifier::Uniform);
  uniform.variable.setName(programBuilder->nameVariable(name));
  uniform.visibility = visibility;
  uniforms.push_back(uniform);
  return uniform.variable.name();
}

SamplerHandle GLUniformHandler::internalAddSampler(const TextureSampler* sampler,
                                                   const std::string& name) {
  auto mangledName = programBuilder->nameVariable(name);
  auto caps = GLCaps::Get(programBuilder->getContext());
  const auto& swizzle = caps->getReadSwizzle(sampler->format);

  SLType type;
  switch (static_cast<const GLSampler*>(sampler)->target) {
    case GL_TEXTURE_EXTERNAL_OES:
      programBuilder->fragmentShaderBuilder()->addFeature(PrivateFeature::OESTexture,
                                                          "GL_OES_EGL_image_external");
      type = SLType::TextureExternalSampler;
      break;
    case GL_TEXTURE_RECTANGLE:
      type = SLType::Texture2DRectSampler;
      break;
    default:
      type = SLType::Texture2DSampler;
      break;
  }
  GLUniform samplerUniform;
  samplerUniform.variable.setType(type);
  samplerUniform.variable.setTypeModifier(ShaderVar::TypeModifier::Uniform);
  samplerUniform.variable.setName(mangledName);
  samplerUniform.visibility = ShaderFlags::Fragment;
  samplerSwizzles.push_back(swizzle);
  samplers.push_back(samplerUniform);
  return SamplerHandle(samplers.size() - 1);
}

std::string GLUniformHandler::getUniformDeclarations(ShaderFlags visibility) const {
  std::string ret;
  for (auto& uniform : uniforms) {
    if ((uniform.visibility & visibility) == visibility) {
      ret += programBuilder->getShaderVarDeclarations(uniform.variable, visibility);
      ret += ";\n";
    }
  }
  for (const auto& sampler : samplers) {
    if ((sampler.visibility & visibility) == visibility) {
      ret += programBuilder->getShaderVarDeclarations(sampler.variable, visibility);
      ret += ";\n";
    }
  }
  return ret;
}

void GLUniformHandler::resolveUniformLocations(unsigned programID) {
  auto gl = GLFunctions::Get(programBuilder->getContext());
  for (auto& uniform : uniforms) {
    uniform.location = gl->getUniformLocation(programID, uniform.variable.name().c_str());
  }
  for (auto& sampler : samplers) {
    sampler.location = gl->getUniformLocation(programID, sampler.variable.name().c_str());
  }
}

std::unique_ptr<GLUniformBuffer> GLUniformHandler::makeUniformBuffer() const {
  std::vector<Uniform> uniformList = {};
  std::vector<int> locations = {};
  for (auto& uniform : uniforms) {
    std::optional<Uniform::Type> type;
    switch (uniform.variable.type()) {
      case SLType::Float:
        type = Uniform::Type::Float;
        break;
      case SLType::Float2:
        type = Uniform::Type::Float2;
        break;
      case SLType::Float3:
        type = Uniform::Type::Float3;
        break;
      case SLType::Float4:
        type = Uniform::Type::Float4;
        break;
      case SLType::Float2x2:
        type = Uniform::Type::Float2x2;
        break;
      case SLType::Float3x3:
        type = Uniform::Type::Float3x3;
        break;
      case SLType::Float4x4:
        type = Uniform::Type::Float4x4;
        break;
      case SLType::Int:
        type = Uniform::Type::Int;
        break;
      case SLType::Int2:
        type = Uniform::Type::Int2;
        break;
      case SLType::Int3:
        type = Uniform::Type::Int3;
        break;
      case SLType::Int4:
        type = Uniform::Type::Int4;
        break;
      case SLType::UByte4Color:
        type = Uniform::Type::Float4;
        break;
      default:
        type = std::nullopt;
        break;
    }
    if (type.has_value()) {
      uniformList.push_back({uniform.variable.name(), *type});
      locations.push_back(uniform.location);
    }
  }
  return std::make_unique<GLUniformBuffer>(std::move(uniformList), std::move(locations));
}
}  // namespace tgfx
