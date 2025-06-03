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

std::shared_ptr<Data> ShaderSerialization::Serialize(const Shader* shader,
                                                     SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  DEBUG_ASSERT(shader != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerSubAttribute", startMap, contentMap);
  auto type = Types::Get(shader);
  switch (type) {
    case Types::ShaderType::Color:
      SerializeColorShaderImpl(fbb, shader, map);
      break;
    case Types::ShaderType::ColorFilter:
      SerializeColorFilterShaderImpl(fbb, shader, map, rosMap);
      break;
    case Types::ShaderType::Image:
      SerializeImageShaderImpl(fbb, shader, map, rosMap);
      break;
    case Types::ShaderType::Blend:
      SerializeBlendShaderImpl(fbb, shader, map, rosMap);
      break;
    case Types::ShaderType::Matrix:
      SerializeMatrixShaderImpl(fbb, shader, map, rosMap);
    case Types::ShaderType::Gradient:
      SerializeGradientShaderImpl(fbb, shader, map);
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void ShaderSerialization::SerializeBasicShaderImpl(flexbuffers::Builder& fbb,
                                                   const Shader* shader) {
  SerializeUtils::SetFlexBufferMap(fbb, "type",
                                   SerializeUtils::ShaderTypeToString(Types::Get(shader)));
}

void ShaderSerialization::SerializeColorShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                                   SerializeUtils::ComplexObjSerMap* map) {
  SerializeBasicShaderImpl(fbb, shader);
  const ColorShader* colorShader = static_cast<const ColorShader*>(shader);

  auto colorID = SerializeUtils::GetObjID();
  auto color = colorShader->color;
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillComplexObjSerMap(color, colorID, map);
}

void ShaderSerialization::SerializeColorFilterShaderImpl(flexbuffers::Builder& fbb,
                                                         const Shader* shader,
                                                         SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  SerializeBasicShaderImpl(fbb, shader);
  const ColorFilterShader* colorFilterShader = static_cast<const ColorFilterShader*>(shader);

  {
    auto shaderID = SerializeUtils::GetObjID();
    auto shader = colorFilterShader->shader;
    SerializeUtils::SetFlexBufferMap(fbb, "shader", reinterpret_cast<uint64_t>(shader.get()), true,
                                     shader != nullptr, shaderID);
    SerializeUtils::FillComplexObjSerMap(shader, shaderID, map, rosMap);
  }

  auto colorFilterID = SerializeUtils::GetObjID();
  auto colorFilter = colorFilterShader->colorFilter;
  SerializeUtils::SetFlexBufferMap(fbb, "colorFilter",
                                   reinterpret_cast<uint64_t>(colorFilter.get()), true,
                                   colorFilter != nullptr, colorFilterID);
  SerializeUtils::FillComplexObjSerMap(colorFilter, colorFilterID, map);
}

void ShaderSerialization::SerializeImageShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                                   SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  SerializeBasicShaderImpl(fbb, shader);
  const ImageShader* imageShader = static_cast<const ImageShader*>(shader);

  auto imageID = SerializeUtils::GetObjID();
  auto image = imageShader->image;
  SerializeUtils::SetFlexBufferMap(fbb, "image", reinterpret_cast<uint64_t>(image.get()), true,
                                   image != nullptr, imageID, image != nullptr);
  SerializeUtils::FillComplexObjSerMap(image, imageID, map);
    SerializeUtils::FillRenderableObjSerMap(image, imageID, rosMap);
  SerializeUtils::SetFlexBufferMap(fbb, "tileModeX",
                                   SerializeUtils::TileModeToString(imageShader->tileModeX));
  SerializeUtils::SetFlexBufferMap(fbb, "tileModeY",
                                   SerializeUtils::TileModeToString(imageShader->tileModeY));

  auto samplingID = SerializeUtils::GetObjID();
  auto sampling = imageShader->sampling;
  SerializeUtils::SetFlexBufferMap(fbb, "sampling", "", false, true, samplingID);
  SerializeUtils::FillComplexObjSerMap(sampling, samplingID, map);
}

void ShaderSerialization::SerializeBlendShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                                   SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  SerializeBasicShaderImpl(fbb, shader);
  const BlendShader* blendShader = static_cast<const BlendShader*>(shader);
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(blendShader->mode));

  auto dstID = SerializeUtils::GetObjID();
  auto dst = blendShader->dst;
  SerializeUtils::SetFlexBufferMap(fbb, "dst", reinterpret_cast<uint64_t>(dst.get()), true,
                                   dst != nullptr, dstID);
  SerializeUtils::FillComplexObjSerMap(dst, dstID, map, rosMap);

  auto srcID = SerializeUtils::GetObjID();
  auto src = blendShader->src;
  SerializeUtils::SetFlexBufferMap(fbb, "src", reinterpret_cast<uint64_t>(src.get()), true,
                                   src != nullptr, srcID);
  SerializeUtils::FillComplexObjSerMap(src, srcID, map, rosMap);
}

void ShaderSerialization::SerializeMatrixShaderImpl(flexbuffers::Builder& fbb, const Shader* shader,
                                                    SerializeUtils::ComplexObjSerMap* map, SerializeUtils::RenderableObjSerMap* rosMap) {
  SerializeBasicShaderImpl(fbb, shader);
  const MatrixShader* matrixShader = static_cast<const MatrixShader*>(shader);

  auto sourceID = SerializeUtils::GetObjID();
  auto source = matrixShader->source;
  SerializeUtils::SetFlexBufferMap(fbb, "source", reinterpret_cast<uint64_t>(source.get()), true,
                                   source != nullptr, sourceID);
  SerializeUtils::FillComplexObjSerMap(source, sourceID, map, rosMap);

  auto matrixID = SerializeUtils::GetObjID();
  auto matrix = matrixShader->matrix;
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", "", false, true, matrixID);
  SerializeUtils::FillComplexObjSerMap(matrix, matrixID, map);
}

void ShaderSerialization::SerializeGradientShaderImpl(flexbuffers::Builder& fbb,
                                                      const Shader* shader,
                                                      SerializeUtils::ComplexObjSerMap* map) {
  SerializeBasicShaderImpl(fbb, shader);
  const GradientShader* gradientShader = static_cast<const GradientShader*>(shader);

  auto originalColorsID = SerializeUtils::GetObjID();
  auto originalColors = gradientShader->originalColors;
  auto originalColorsSize = static_cast<unsigned int>(originalColors.size());
  SerializeUtils::SetFlexBufferMap(fbb, "originalColors", originalColorsSize, false,
                                   originalColorsSize, originalColorsID);
  SerializeUtils::FillComplexObjSerMap(originalColors, originalColorsID, map);

  auto originalPositionsID = SerializeUtils::GetObjID();
  auto originalPositions = gradientShader->originalPositions;
  auto originalPositionSize = static_cast<unsigned int>(originalPositions.size());
  SerializeUtils::SetFlexBufferMap(fbb, "orignalPositions", originalPositionSize, false,
                                   originalPositionSize, originalPositionsID);
  SerializeUtils::FillComplexObjSerMap(originalPositions, originalPositionsID, map);

  auto pointsToUnit = gradientShader->pointsToUnit;
  auto pointsToUnitID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "pointsToUnit", "", false, true, pointsToUnitID);
  SerializeUtils::FillComplexObjSerMap(pointsToUnit, pointsToUnitID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "colorsAreOpaque", gradientShader->colorsAreOpaque);
}
}  // namespace tgfx
#endif