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
  auto* fragBuilder = args.fragBuilder;
  auto common = args.uniformHandler->addUniform("GlassOpticsP0", UniformFormat::Float4,
                                                ShaderStage::Fragment);
  auto lighting = args.uniformHandler->addUniform("GlassOpticsP1", UniformFormat::Float4,
                                                  ShaderStage::Fragment);
  auto offsets = args.uniformHandler->addUniform("GlassOpticsP2", UniformFormat::Float4,
                                                 ShaderStage::Fragment);
  auto geometryMapping = args.uniformHandler->addUniform("GlassOpticsP3", UniformFormat::Float4,
                                                         ShaderStage::Fragment);
  auto& sourceSampler = (*args.textureSamplers)[0];
  auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);

  fragBuilder->codeAppendf("vec2 sourceUV = %s * %s.xy;", texCoordName.c_str(), common.c_str());
  fragBuilder->codeAppendf("vec2 glassUV = %s * %s.xy + %s.xy;", texCoordName.c_str(),
                           offsets.c_str(), geometryMapping.c_str());
  std::string geometryOutput = "glassGeometry";
  emitChild(geometryIndex, "vec4(glassUV, 0.0, 0.0)", &geometryOutput, args);
  fragBuilder->codeAppendf("vec2 refractDir = %s.xy;", geometryOutput.c_str());
  fragBuilder->codeAppendf("float offsetDist = %s.z;", geometryOutput.c_str());
  fragBuilder->codeAppendf("float edgeWeight = %s.w;", geometryOutput.c_str());
  fragBuilder->codeAppend("vec2 displacement = refractDir * offsetDist;");
  fragBuilder->codeAppendf("displacement = clamp(displacement, vec2(-%s.w), vec2(%s.w));",
                           common.c_str(), common.c_str());
  fragBuilder->codeAppendf("vec2 uvOffset = vec2(displacement.x * %s.x, -displacement.y * %s.y);",
                           lighting.c_str(), lighting.c_str());

  fragBuilder->codeAppend("vec3 finalColor;");
  fragBuilder->codeAppend("float srcAlpha;");
  if (params.dispersion >= 0.01f) {
    fragBuilder->codeAppendf("vec2 uvR = sourceUV + uvOffset * (1.0 + %s.z);", common.c_str());
    fragBuilder->codeAppend("vec2 uvG = sourceUV + uvOffset;");
    fragBuilder->codeAppendf("vec2 uvB = sourceUV + uvOffset * (1.0 - %s.z);", common.c_str());
    fragBuilder->codeAppend("vec4 srcG = ");
    fragBuilder->appendTextureLookup(sourceSampler, "uvG");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("finalColor.r = ");
    fragBuilder->appendTextureLookup(sourceSampler, "uvR");
    fragBuilder->codeAppend(".r;");
    fragBuilder->codeAppend("finalColor.g = srcG.g;");
    fragBuilder->codeAppend("finalColor.b = ");
    fragBuilder->appendTextureLookup(sourceSampler, "uvB");
    fragBuilder->codeAppend(".b;");
    fragBuilder->codeAppend("srcAlpha = srcG.a;");
  } else {
    fragBuilder->codeAppend("vec4 srcColor = ");
    fragBuilder->appendTextureLookup(sourceSampler, "sourceUV + uvOffset");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("finalColor = srcColor.rgb;");
    fragBuilder->codeAppend("srcAlpha = srcColor.a;");
  }

  if (params.lightIntensity > 0.0f) {
    fragBuilder->codeAppend("if (edgeWeight > 0.0) {");
    fragBuilder->codeAppendf("  float NdotL = dot(-refractDir, %s.zw);", lighting.c_str());
    fragBuilder->codeAppendf("  float diffuse = smoothstep(0.35, 1.0, NdotL) * edgeWeight * %s.z;",
                             offsets.c_str());
    fragBuilder->codeAppendf(
        "  float rim = smoothstep(0.35, 1.0, -NdotL) * edgeWeight * %s.z * 0.6;", offsets.c_str());
    fragBuilder->codeAppend("  finalColor += vec3(diffuse + rim);");
    fragBuilder->codeAppend("}");
  }
  // DEBUG: switch the display mode by the sign of the light direction X component. A positive
  // X (light from the right) shows the refraction direction as a color over the whole glass,
  // a negative X keeps the normal rendering.
  fragBuilder->codeAppendf("  if (%s.z >= 0.0) { finalColor = vec3(refractDir * 0.5 + 0.5, 0.5); }",
                           lighting.c_str());
  fragBuilder->codeAppendf("%s = vec4(finalColor, srcAlpha);", args.outputColor.c_str());
}

void GLSLGlassRefractionFragmentProcessor::onSetData(UniformData*,
                                                     UniformData* fragmentUniformData) const {
  float sourceWidth = static_cast<float>(sourceProxy->width());
  float sourceHeight = static_cast<float>(sourceProxy->height());
  float commonData[4] = {1.0f / sourceWidth, 1.0f / sourceHeight, params.dispersion,
                         params.maxDisplacement};
  fragmentUniformData->setData("GlassOpticsP0", commonData);

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
