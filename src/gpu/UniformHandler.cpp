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
static constexpr char OES_TEXTURE_EXTENSION[] = "GL_OES_EGL_image_external_essl3";

std::string UniformHandler::addUniform(const std::string& name, UniformFormat format,
                                       ShaderStage stage, uint32_t count) {
  auto uniformName = programBuilder->nameVariable(name);
  // Operators that share one uniform set (the blur's kernel slots feed both the whole-kernel
  // call and the in-loop weight) each run their EnsureUniforms; a name already registered for
  // this processor is that same uniform, not a second one. Deduplicating here keeps the block
  // layout matching the single upload the data side writes.
  auto& uniforms = stage == ShaderStage::Vertex ? vertexUniforms : fragmentUniforms;
  for (const auto& uniform : uniforms) {
    if (uniform.name() == uniformName) {
      return uniformName;
    }
  }
  uniforms.emplace_back(uniformName, format, count);
  return uniformName;
}

SamplerHandle UniformHandler::addSampler(std::shared_ptr<Texture> texture,
                                         const std::string& name) {
  // The same texture can be added multiple times, each with a different name.
  UniformFormat format;
  switch (texture->type()) {
    case TextureType::External:
      programBuilder->fragmentShaderBuilder()->addFeature(PrivateFeature::OESTexture,
                                                          OES_TEXTURE_EXTENSION);
      format = UniformFormat::TextureExternalSampler;
      break;
    case TextureType::Rectangle:
      format = UniformFormat::Texture2DRectSampler;
      break;
    default:
      format = UniformFormat::Texture2DSampler;
      break;
  }
  auto samplerName = programBuilder->nameVariable(name);
  samplers.emplace_back(samplerName, format);
  auto swizzle = Swizzle::ForRead(texture->format());
  samplerSwizzles.push_back(swizzle);
  return SamplerHandle(samplers.size() - 1);
}

ShaderVar UniformHandler::getSamplerVariable(SamplerHandle handle) const {
  auto& uniform = samplers[handle.toIndex()];
  return ShaderVar(uniform);
}

}  // namespace tgfx
