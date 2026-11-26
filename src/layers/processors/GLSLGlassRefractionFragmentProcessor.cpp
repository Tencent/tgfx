/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLGlassRefractionFragmentProcessor.h"
#include <cmath>
#include "GeneratedOperators.h"

namespace tgfx {

PlacementPtr<GlassRefractionFragmentProcessor> GlassRefractionFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix) {
  if (allocator == nullptr || source == nullptr || geometry == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassRefractionFragmentProcessor>(
      std::move(source), std::move(geometry), params, coordMatrix);
}

GLSLGlassRefractionFragmentProcessor::GLSLGlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source,
    PlacementPtr<GlassShapeGeometryFragmentProcessor> geometry, const GlassRefractionParams& params,
    const Matrix& coordMatrix)
    : GlassRefractionFragmentProcessor(std::move(source), std::move(geometry), params,
                                       coordMatrix) {
}

void GLSLGlassRefractionFragmentProcessor::emitCode(EmitArgs& args) const {
  // The IR route is the only route: the displacement math, the source reads, the edge light and
  // the final assembly live in the shared function, and the compile-time shape/effect flags ride
  // the call as int constants.
  if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  auto ir = args.irBuilder;
  const auto sampler = args.samplerValue((*args.textureSamplers)[0]);
  const auto texCoordName = args.fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  if (sampler == INVALID_VALUE_ID || texCoordName.empty()) {
    ir->MarkFailed("the glass refraction inputs have no IR values");
    return;
  }
  GlassRefractionOp::UniformNames names = {};
  GlassRefractionOp::EnsureUniforms(ir, args.uniformHandler, &names);
  const auto texCoord =
      ir->InternValue(texCoordName, SLType::Float2, Precision::High, IRValueKind::Varying);
  const auto offsets =
      ir->InternValue(names.GlassOpticsP2, SLType::Float4, Precision::High, IRValueKind::Uniform);
  const auto mapping =
      ir->InternValue(names.GlassOpticsP3, SLType::Float4, Precision::High, IRValueKind::Uniform);
  const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, texCoord,
                                       ir->CreateSwizzle(offsets, SLType::Float2, "xy"));
  const auto glassUV =
      ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, scaled,
                       ir->CreateSwizzle(mapping, SLType::Float2, "xy"));
  const auto zero = ir->CreateConstant(0.0f);
  const auto geometryInput = ir->CreateConstruct(SLType::Float4, {glassUV, zero, zero});
  std::string geometryName = "glassGeometry";
  const auto geometry =
      emitChildIR(geometryIndex, "glassGeometryInput", &geometryName, args, geometryInput);
  if (geometry == INVALID_VALUE_ID) {
    ir->MarkFailed("the glass geometry child has no IR output");
    return;
  }
  const int32_t udfShape = params.shapeType == GlassShapeType::AlphaMask ? 1 : 0;
  const int32_t dispersionOn = params.dispersion >= 0.01f ? 1 : 0;
  const int32_t lightingOn = params.lightIntensity > 0.0f ? 1 : 0;
  ir->StoreOutput(args.outputValue,
                  GlassRefractionOp::Emit(ir, args.uniformHandler, &names, sampler, texCoord,
                                          geometry, udfShape, dispersionOn, lightingOn));
}

void GLSLGlassRefractionFragmentProcessor::onSetData(UniformData*,
                                                     UniformData* fragmentUniformData) const {
  float sourceWidth = static_cast<float>(sourceProxy->width());
  float sourceHeight = static_cast<float>(sourceProxy->height());
  float commonData[4] = {1.0f / sourceWidth, 1.0f / sourceHeight, params.dispersion,
                         params.maxDisplacement};
  fragmentUniformData->setData("GlassOpticsP0", commonData);

  // The precompiled refraction kernel dropped the directional-light angle term, so the runtime
  // emission matches it with a fixed head-on light instead of reviving the old field.
  float angle = params.lightAngle * static_cast<float>(M_PI) / 180.0f;
  float layerToSourceX = params.layerPixelToSourcePixelX;
  float layerToSourceY = params.layerPixelToSourcePixelY;
  if (layerToSourceX <= 0.0f) {
    layerToSourceX = params.origWidth > 0.0f ? sourceWidth / params.origWidth : 1.0f;
  }
  if (layerToSourceY <= 0.0f) {
    layerToSourceY = params.origHeight > 0.0f ? sourceHeight / params.origHeight : 1.0f;
  }
  float lightingData[4] = {layerToSourceX / sourceWidth, layerToSourceY / sourceHeight,
                           std::sin(angle), std::cos(angle)};
  fragmentUniformData->setData("GlassOpticsP1", lightingData);

  float glassUVScaleX = params.glassUVScaleX > 0.0f ? params.glassUVScaleX : 1.0f / sourceWidth;
  float glassUVScaleY = params.glassUVScaleY > 0.0f ? params.glassUVScaleY : 1.0f / sourceHeight;
  float offsetData[4] = {glassUVScaleX, glassUVScaleY, params.lightIntensity,
                         params.frost / 100.0f};
  fragmentUniformData->setData("GlassOpticsP2", offsetData);
  float geometryMappingData[4] = {params.glassUVOffsetX, params.glassUVOffsetY, 0.0f, 0.0f};
  fragmentUniformData->setData("GlassOpticsP3", geometryMappingData);
}

}  // namespace tgfx
