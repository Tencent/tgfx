/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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
#ifdef TGFX_USE_INSPECTOR

#include "ShaderSerialization.h"
#include "core/shaders/BlendShader.h"
#include "core/shaders/ColorFilterShader.h"
#include "core/shaders/ColorShader.h"
#include "core/shaders/GradientShader.h"
#include "core/shaders/ImageShader.h"
#include "core/shaders/MatrixShader.h"

namespace tgfx {

std::shared_ptr<Data> ShaderSerialization::serializeShader(Shader* shader) {
  DEBUG_ASSERT(shader != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = shader->type();
  switch (type) {
    case Shader::Type::Color:
      serializeColorShaderImpl(fbb, shader);
      break;
    case Shader::Type::ColorFilter:
      serializeColorFilterShaderImpl(fbb, shader);
      break;
    case Shader::Type::Image:
      serializeImageShaderImpl(fbb, shader);
      break;
    case Shader::Type::Blend:
      serializeBlendShaderImpl(fbb, shader);
      break;
    case Shader::Type::Matrix:
      serializeMatrixShaderImpl(fbb, shader);
    case Shader::Type::Gradient:
      serializeGradientShaderImpl(fbb, shader);
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ShaderSerialization::serializeBasicShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeUtils::setFlexBufferMap(fbb, "Type", SerializeUtils::shaderTypeToString(shader->type()));
}
void ShaderSerialization::serializeColorShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  serializeBasicShaderImpl(fbb, shader);
  ColorShader* colorShader = static_cast<ColorShader*>(shader);
  (void)colorShader;
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
}
void ShaderSerialization::serializeColorFilterShaderImpl(flexbuffers::Builder& fbb,
                                                         Shader* shader) {
  serializeBasicShaderImpl(fbb, shader);
  ColorFilterShader* colorFilterShader = static_cast<ColorFilterShader*>(shader);
  SerializeUtils::setFlexBufferMap(fbb, "Shader",
                                   reinterpret_cast<uint64_t>(colorFilterShader->shader.get()),
                                   true, colorFilterShader->shader != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "ColorFilter",
                                   reinterpret_cast<uint64_t>(colorFilterShader->shader.get()),
                                   true, colorFilterShader->shader != nullptr);
}
void ShaderSerialization::serializeImageShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  serializeBasicShaderImpl(fbb, shader);
  ImageShader* imageShader = static_cast<ImageShader*>(shader);
  SerializeUtils::setFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(imageShader->image.get()), true,
                                   imageShader->image != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "TileModeX",
                                   SerializeUtils::tileModeToString(imageShader->tileModeX));
  SerializeUtils::setFlexBufferMap(fbb, "TileModeY",
                                   SerializeUtils::tileModeToString(imageShader->tileModeY));
  SerializeUtils::setFlexBufferMap(fbb, "Sampling", "", false, true);
}

void ShaderSerialization::serializeBlendShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  serializeBasicShaderImpl(fbb, shader);
  BlendShader* blendShader = static_cast<BlendShader*>(shader);
  SerializeUtils::setFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::blendModeToString(blendShader->mode));
  SerializeUtils::setFlexBufferMap(fbb, "Dst", reinterpret_cast<uint64_t>(blendShader->dst.get()),
                                   true, blendShader->dst != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Src", reinterpret_cast<uint64_t>(blendShader->src.get()),
                                   true, blendShader->src != nullptr);
}

void ShaderSerialization::serializeMatrixShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  serializeBasicShaderImpl(fbb, shader);
  MatrixShader* matrixShader = static_cast<MatrixShader*>(shader);
  SerializeUtils::setFlexBufferMap(fbb, "Source",
                                   reinterpret_cast<uint64_t>(matrixShader->source.get()), true,
                                   matrixShader->source != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Matrix", "", false, true);
}

void ShaderSerialization::serializeGradientShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  serializeBasicShaderImpl(fbb, shader);
  GradientShader* gradientShader = static_cast<GradientShader*>(shader);
  auto originalColorsSize = static_cast<unsigned int>(gradientShader->originalColors.size());
  SerializeUtils::setFlexBufferMap(fbb, "OriginalColors", originalColorsSize, false,
                                   originalColorsSize);
  auto originalPositionSize = static_cast<unsigned int>(gradientShader->originalPositions.size());
  SerializeUtils::setFlexBufferMap(fbb, "OrignalPositions", originalPositionSize, false,
                                   originalPositionSize);
  SerializeUtils::setFlexBufferMap(fbb, "PointsToUnit", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "ColorsAreOpaque", gradientShader->colorsAreOpaque);
}

}  // namespace tgfx
#endif