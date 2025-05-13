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

std::shared_ptr<Data> ShaderSerialization::Serialize(Shader* shader) {
  DEBUG_ASSERT(shader != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(shader);
  switch (type) {
    case Types::ShaderType::Color:
      SerializeColorShaderImpl(fbb, shader);
      break;
    case Types::ShaderType::ColorFilter:
      SerializeColorFilterShaderImpl(fbb, shader);
      break;
    case Types::ShaderType::Image:
      SerializeImageShaderImpl(fbb, shader);
      break;
    case Types::ShaderType::Blend:
      SerializeBlendShaderImpl(fbb, shader);
      break;
    case Types::ShaderType::Matrix:
      SerializeMatrixShaderImpl(fbb, shader);
    case Types::ShaderType::Gradient:
      SerializeGradientShaderImpl(fbb, shader);
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ShaderSerialization::SerializeBasicShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeUtils::SetFlexBufferMap(fbb, "Type",
                                   SerializeUtils::ShaderTypeToString(Types::Get(shader)));
}
void ShaderSerialization::SerializeColorShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeBasicShaderImpl(fbb, shader);
  ColorShader* colorShader = static_cast<ColorShader*>(shader);
  (void)colorShader;
  SerializeUtils::SetFlexBufferMap(fbb, "Color", "", false, true);
}
void ShaderSerialization::SerializeColorFilterShaderImpl(flexbuffers::Builder& fbb,
                                                         Shader* shader) {
  SerializeBasicShaderImpl(fbb, shader);
  ColorFilterShader* colorFilterShader = static_cast<ColorFilterShader*>(shader);
  SerializeUtils::SetFlexBufferMap(fbb, "Shader",
                                   reinterpret_cast<uint64_t>(colorFilterShader->shader.get()),
                                   true, colorFilterShader->shader != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "ColorFilter",
                                   reinterpret_cast<uint64_t>(colorFilterShader->shader.get()),
                                   true, colorFilterShader->shader != nullptr);
}
void ShaderSerialization::SerializeImageShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeBasicShaderImpl(fbb, shader);
  ImageShader* imageShader = static_cast<ImageShader*>(shader);
  SerializeUtils::SetFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(imageShader->image.get()), true,
                                   imageShader->image != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "TileModeX",
                                   SerializeUtils::TileModeToString(imageShader->tileModeX));
  SerializeUtils::SetFlexBufferMap(fbb, "TileModeY",
                                   SerializeUtils::TileModeToString(imageShader->tileModeY));
  SerializeUtils::SetFlexBufferMap(fbb, "Sampling", "", false, true);
}

void ShaderSerialization::SerializeBlendShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeBasicShaderImpl(fbb, shader);
  BlendShader* blendShader = static_cast<BlendShader*>(shader);
  SerializeUtils::SetFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::BlendModeToString(blendShader->mode));
  SerializeUtils::SetFlexBufferMap(fbb, "Dst", reinterpret_cast<uint64_t>(blendShader->dst.get()),
                                   true, blendShader->dst != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "Src", reinterpret_cast<uint64_t>(blendShader->src.get()),
                                   true, blendShader->src != nullptr);
}

void ShaderSerialization::SerializeMatrixShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeBasicShaderImpl(fbb, shader);
  MatrixShader* matrixShader = static_cast<MatrixShader*>(shader);
  SerializeUtils::SetFlexBufferMap(fbb, "Source",
                                   reinterpret_cast<uint64_t>(matrixShader->source.get()), true,
                                   matrixShader->source != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "Matrix", "", false, true);
}

void ShaderSerialization::SerializeGradientShaderImpl(flexbuffers::Builder& fbb, Shader* shader) {
  SerializeBasicShaderImpl(fbb, shader);
  GradientShader* gradientShader = static_cast<GradientShader*>(shader);
  auto originalColorsSize = static_cast<unsigned int>(gradientShader->originalColors.size());
  SerializeUtils::SetFlexBufferMap(fbb, "OriginalColors", originalColorsSize, false,
                                   originalColorsSize);
  auto originalPositionSize = static_cast<unsigned int>(gradientShader->originalPositions.size());
  SerializeUtils::SetFlexBufferMap(fbb, "OrignalPositions", originalPositionSize, false,
                                   originalPositionSize);
  SerializeUtils::SetFlexBufferMap(fbb, "PointsToUnit", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "ColorsAreOpaque", gradientShader->colorsAreOpaque);
}

}  // namespace tgfx
#endif