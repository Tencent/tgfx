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
    std::shared_ptr<TextureProxy> fineMask, std::shared_ptr<TextureProxy> coarseMask,
    const GlassRefractionParams& params, const Matrix& coordMatrix) {
  if (allocator == nullptr || source == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassRefractionFragmentProcessor>(
      std::move(source), std::move(fineMask), std::move(coarseMask), params, coordMatrix);
}

GLSLGlassRefractionFragmentProcessor::GLSLGlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source, std::shared_ptr<TextureProxy> fineMask,
    std::shared_ptr<TextureProxy> coarseMask, const GlassRefractionParams& params,
    const Matrix& coordMatrix)
    : GlassRefractionFragmentProcessor(std::move(source), std::move(fineMask),
                                       std::move(coarseMask), params, coordMatrix) {
}

void GLSLGlassRefractionFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;
  const bool hasMask = (fineMaskProxy != nullptr);
  const bool hasCoarseMask = (coarseMaskProxy != nullptr);
  const bool hasDispersion = (params.dispersion >= 0.01f);
  const bool hasLightIntensity = (params.lightIntensity > 0.0f);
  const bool useAxisMix = (params.shapeType == GlassShapeType::RoundedRect ||
                           params.shapeType == GlassShapeType::Ellipse);

  // Register uniforms: 6 x vec4.
  // uvTransform: glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
  auto uvTransform =
      uniformHandler->addUniform("GlassP0", UniformFormat::Float4, ShaderStage::Fragment);
  // shapeParams: halfW, halfH, cornerRadius, minHalf
  auto shapeParams =
      uniformHandler->addUniform("GlassP1", UniformFormat::Float4, ShaderStage::Fragment);
  // glassThicknessParam: invOrigW(.x), invOrigH(.y), lightDirX(.z), glassThickness(.w)
  auto glassThicknessParam =
      uniformHandler->addUniform("GlassP2", UniformFormat::Float4, ShaderStage::Fragment);
  // refractionParams: refractionFactor, dispersion, invSourceW, invSourceH
  auto refractionParams =
      uniformHandler->addUniform("GlassP3", UniformFormat::Float4, ShaderStage::Fragment);
  // lightParams: splay, depthRatio, lightDirY, lightIntensity
  auto lightParams =
      uniformHandler->addUniform("GlassP4", UniformFormat::Float4, ShaderStage::Fragment);
  // miscParams: origMinHalf, udfPixelToLayerPixel, renderOffsetX, renderOffsetY
  auto miscParams =
      uniformHandler->addUniform("GlassP5", UniformFormat::Float4, ShaderStage::Fragment);

  // Get texture samplers.
  auto& sourceSampler = (*args.textureSamplers)[0];

  // Get pixel coordinate from CoordTransform (Identity, no textureProxy → pixel coords).
  auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);

  // --- Emit SDF helper functions via addFunction (placed before main) ---
  std::string outerSdfFn;
  if (useAxisMix) {
    if (params.shapeType == GlassShapeType::RoundedRect) {
      outerSdfFn = fragBuilder->getMangledFunctionName("glass_outerSDF");

      fragBuilder->addFunction(
          "float " + outerSdfFn +
          "(float px, float py, float hw, float hh, float r) {\n"
          "  float distX = abs(px) - hw + r;\n"
          "  float distY = abs(py) - hh + r;\n"
          "  float outerDist = sqrt(max(distX,0.0)*max(distX,0.0)+max(distY,0.0)*max(distY,0.0));\n"
          "  return outerDist + min(max(distX,distY),0.0) - r;\n"
          "}\n");
    } else {
      // Ellipse
      outerSdfFn = fragBuilder->getMangledFunctionName("glass_outerSDF");

      fragBuilder->addFunction(
          "float " + outerSdfFn +
          "(float px, float py, float hw, float hh) {\n"
          "  float normalizedDist = length(vec2(px/hw, py/hh));\n"
          "  float gradientLength = length(vec2(px/(hw*hw), py/(hh*hh)));\n"
          "  if (gradientLength < 0.000001) return -min(hw, hh);\n"
          "  return normalizedDist * (normalizedDist - 1.0) / gradientLength;\n"
          "}\n");
    }
  }

  // --- Main shader body: coordinate conversion first ---
  fragBuilder->codeAppendf("float halfW = %s.x;", shapeParams.c_str());
  fragBuilder->codeAppendf("float halfH = %s.y;", shapeParams.c_str());
  fragBuilder->codeAppendf("float invSourceW = %s.z;", refractionParams.c_str());
  fragBuilder->codeAppendf("float invSourceH = %s.w;", refractionParams.c_str());
  // Add render offset to convert from render target pixel coords to source pixel coords.
  fragBuilder->codeAppendf("vec2 sourceUV = (%s + %s.zw) * vec2(invSourceW, invSourceH);",
                           texCoordName.c_str(), miscParams.c_str());

  // Convert source UV to glass pixel coords centered at origin.
  fragBuilder->codeAppendf("vec2 glassUV = (sourceUV - %s.xy) * %s.zw;", uvTransform.c_str(),
                           uvTransform.c_str());
  fragBuilder->codeAppend("glassUV = vec2(glassUV.x, 1.0 - glassUV.y);");
  fragBuilder->codeAppend("vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);");
  fragBuilder->codeAppend("float px = glassPixel.x - halfW;");
  fragBuilder->codeAppend("float py = glassPixel.y - halfH;");

  // Evaluate SDF after px/py are available.
  if (useAxisMix) {
    if (params.shapeType == GlassShapeType::RoundedRect) {
      fragBuilder->codeAppendf("float cornerRadius = %s.z;", shapeParams.c_str());
      fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH, cornerRadius);",
                               outerSdfFn.c_str());
    } else {
      fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH);", outerSdfFn.c_str());
    }
  }

  fragBuilder->codeAppend("vec2 uvOffset = vec2(0.0);");

  // Extract common params.
  fragBuilder->codeAppendf("float minHalf = %s.w;", shapeParams.c_str());
  fragBuilder->codeAppendf("float invOrigW = %s.x;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float invOrigH = %s.y;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float lightDirX = %s.z;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float glassThickness = %s.w;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float refractionFactor = %s.x;", refractionParams.c_str());
  fragBuilder->codeAppendf("float dispersion = %s.y;", refractionParams.c_str());
  fragBuilder->codeAppendf("float splay = %s.x;", lightParams.c_str());
  fragBuilder->codeAppendf("float depthRatio = %s.y;", lightParams.c_str());
  fragBuilder->codeAppendf("float lightDirY = %s.z;", lightParams.c_str());
  fragBuilder->codeAppendf("float lightIntensity = %s.w;", lightParams.c_str());
  fragBuilder->codeAppendf("float origMinHalf = %s.x;", miscParams.c_str());
  fragBuilder->codeAppendf("float udfPixelToLayerPixel = %s.y;", miscParams.c_str());

  // Edge weight and normals.
  fragBuilder->codeAppend("float edgeWeight = 0.0;");
  fragBuilder->codeAppend("vec2 glassNormal = vec2(0.0);");
  fragBuilder->codeAppend("vec2 edgeLightNormal = vec2(0.0);");

  if (useAxisMix) {
    // Analytical SDF refraction path.
    fragBuilder->codeAppend("if (outerSDF < 0.0) {");
    fragBuilder->codeAppend("  float edgeDist = -outerSDF;");
    // Edge band width: depthRatio * origMinHalf, capped at 60 pixels.
    fragBuilder->codeAppend("  float edgeBandWidth = min(depthRatio * origMinHalf, 60.0);");
    fragBuilder->codeAppend("  edgeWeight = 1.0 - smoothstep(0.0, 5.0, edgeDist);");
    fragBuilder->codeAppend("  float edgeFactor = 1.0 - min(edgeDist / edgeBandWidth, 1.0);");
    fragBuilder->codeAppend(
        "  float offsetDist = glassThickness * refractionFactor * edgeFactor * edgeFactor;");
    if (params.shapeType == GlassShapeType::Ellipse) {
      // Ellipse normal from SDF gradient: N = normalize(px/hw², py/hh²); radialDir points to center.
      fragBuilder->codeAppend(
          "  vec2 gradientDir = vec2(px / (halfW * halfW), py / (halfH * halfH));");
      fragBuilder->codeAppend("  float gradientLength = length(gradientDir);");
      fragBuilder->codeAppend("  if (gradientLength > 0.000001) {");
      fragBuilder->codeAppend("    vec2 radialDir = -gradientDir / gradientLength;");
    } else {
      // RoundedRect: radial direction from center.
      fragBuilder->codeAppend("  float centerDistance = sqrt(px * px + py * py);");
      fragBuilder->codeAppend("  if (centerDistance > 0.001) {");
      fragBuilder->codeAppend(
          "    vec2 radialDir = vec2(-px / centerDistance, -py / centerDistance);");
    }
    fragBuilder->codeAppend("    vec2 axisDir;");
    fragBuilder->codeAppend(
        "    if (abs(radialDir.x) > abs(radialDir.y)) { axisDir = vec2(sign(radialDir.x), "
        "0.0); }");
    fragBuilder->codeAppend("    else { axisDir = vec2(0.0, sign(radialDir.y)); }");
    fragBuilder->codeAppend("    float maxAxisDot = max(abs(radialDir.x), abs(radialDir.y));");
    fragBuilder->codeAppend("    float controlValue = depthRatio * splay;");
    fragBuilder->codeAppend("    float axisThreshold = mix(0.9, 1.0, controlValue);");
    fragBuilder->codeAppend("    float axisWeight = smoothstep(axisThreshold, 1.0, maxAxisDot);");
    fragBuilder->codeAppend("    vec2 refractDir = mix(radialDir, axisDir, axisWeight);");
    fragBuilder->codeAppend("    glassNormal = -refractDir;");
    fragBuilder->codeAppend("    float displacementX = refractDir.x * offsetDist;");
    fragBuilder->codeAppend("    float displacementY = refractDir.y * offsetDist;");
    fragBuilder->codeAppend(
        "    uvOffset = vec2(displacementX * invOrigW, -displacementY * invOrigH);");
    fragBuilder->codeAppend("  }");
    fragBuilder->codeAppend("}");
  } else if (hasMask) {
    // AlphaMask UDF refraction path.
    auto& maskSampler = (*args.textureSamplers)[1];

    // RGBA8 unpack constants for decoding packed float from 4 channels.
    fragBuilder->codeAppend(
        "const vec4 UNPACK = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);");

    // glassUV already contains the normalized glass coordinates used to derive px/py. Reuse it
    // directly instead of reversing the pixel conversion for every UDF sample.
    fragBuilder->codeAppend("vec2 maskUV = vec2(glassUV.x, 1.0 - glassUV.y);");
    fragBuilder->codeAppend("vec4 packedHeight = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("float height = dot(packedHeight, UNPACK);");

    // Forward difference gradient: sample right and up neighbors.
    fragBuilder->codeAppend(
        "float gradientStep = (depthRatio * 3.0 + 1.0) * udfPixelToLayerPixel;");
    fragBuilder->codeAppend("vec2 gradientUVStep = gradientStep * vec2(0.5 / halfW, 0.5 / halfH);");
    fragBuilder->codeAppend("vec2 maskUVRight = maskUV + vec2(gradientUVStep.x, 0.0);");
    fragBuilder->codeAppend("vec4 packedRight = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUVRight");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("float heightRight = dot(packedRight, UNPACK);");

    fragBuilder->codeAppend("vec2 maskUVUp = maskUV - vec2(0.0, gradientUVStep.y);");
    fragBuilder->codeAppend("vec4 packedUp = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUVUp");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("float heightUp = dot(packedUp, UNPACK);");

    fragBuilder->codeAppend(
        "vec2 gradient = vec2(heightRight - height, heightUp - height) / gradientStep;");
    fragBuilder->codeAppend("float gradientLength = length(gradient);");

    // Coarse UDF only determines the edge-light intensity. The fine UDF direction below is reused
    // as the edge-light normal, avoiding two additional coarse-neighbor texture samples.
    if (hasCoarseMask && hasLightIntensity) {
      auto& coarseSampler = (*args.textureSamplers)[2];
      fragBuilder->codeAppend("vec4 packedEdgeHeight = ");
      fragBuilder->appendTextureLookup(coarseSampler, "maskUV");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend("float edgeLightHeight = dot(packedEdgeHeight, UNPACK);");
      fragBuilder->codeAppend("edgeWeight = 1.0 - smoothstep(0.5, 0.75, edgeLightHeight);");
    }

    // Refraction from fine UDF gradient.
    fragBuilder->codeAppend("if (gradientLength > 0.000001) {");
    fragBuilder->codeAppend("  vec2 gradientDir = gradient / gradientLength;");
    fragBuilder->codeAppend("  float centerDistance = sqrt(px * px + py * py);");
    fragBuilder->codeAppend(
        "  vec2 centerDir = (centerDistance > 0.001) ? vec2(-px / centerDistance, -py / "
        "centerDistance) : vec2(0.0);");
    fragBuilder->codeAppend("  vec2 mixedDir = mix(gradientDir, centerDir, splay);");
    fragBuilder->codeAppend("  mixedDir = normalize(mixedDir);");
    fragBuilder->codeAppend("  glassNormal = -mixedDir;");
    if (hasCoarseMask && hasLightIntensity) {
      fragBuilder->codeAppend("  if (edgeWeight > 0.0) { edgeLightNormal = -mixedDir; }");
    }
    fragBuilder->codeAppend("  float depthScale = smoothstep(0.0, 0.1, depthRatio);");
    fragBuilder->codeAppend(
        "  float refractionDistance = minHalf * refractionFactor * depthRatio * depthScale;");
    fragBuilder->codeAppend("  float edgeProximity = (1.0 - height * height) * (1.0 - height * height);");
    fragBuilder->codeAppend("  float offsetDist = refractionDistance * edgeProximity;");
    fragBuilder->codeAppend("  vec2 displacement = mixedDir * offsetDist;");
    fragBuilder->codeAppend("  vec2 maxDisplacement = vec2(0.999) * refractionDistance;");
    fragBuilder->codeAppend(
        "  displacement = clamp(displacement, -maxDisplacement, maxDisplacement);");
    fragBuilder->codeAppend(
        "  uvOffset = vec2(displacement.x * invOrigW, -displacement.y * invOrigH);");
    fragBuilder->codeAppend("}");
  }

  // Dispersion and source sampling.
  fragBuilder->codeAppend("vec3 finalColor;");
  fragBuilder->codeAppend("float srcAlpha;");
  if (hasDispersion) {
    fragBuilder->codeAppend("  vec2 uvR = sourceUV + uvOffset * (1.0 + dispersion);");
    fragBuilder->codeAppend("  vec2 uvG = sourceUV + uvOffset;");
    fragBuilder->codeAppend("  vec2 uvB = sourceUV + uvOffset * (1.0 - dispersion);");
    fragBuilder->codeAppend("  vec4 srcG = ");
    fragBuilder->appendTextureLookup(sourceSampler, "uvG");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("  finalColor.r = ");
    fragBuilder->appendTextureLookup(sourceSampler, "uvR");
    fragBuilder->codeAppend(".r;");
    fragBuilder->codeAppend("  finalColor.g = srcG.g;");
    fragBuilder->codeAppend("  finalColor.b = ");
    fragBuilder->appendTextureLookup(sourceSampler, "uvB");
    fragBuilder->codeAppend(".b;");
    // Alpha is unaffected by dispersion (only R/B UV offsets differ), so use the G channel's alpha.
    fragBuilder->codeAppend("  srcAlpha = srcG.a;");
  } else {
    fragBuilder->codeAppend("  vec2 sampleUV = sourceUV + uvOffset;");
    fragBuilder->codeAppend("  vec4 srcColor = ");
    fragBuilder->appendTextureLookup(sourceSampler, "sampleUV");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("  finalColor = srcColor.rgb;");
    fragBuilder->codeAppend("  srcAlpha = srcColor.a;");
  }

  // Edge lighting.
  if (hasLightIntensity) {
    fragBuilder->codeAppend("if (edgeWeight > 0.0) {");
    if (useAxisMix) {
      fragBuilder->codeAppend("  vec2 surfaceNormal = glassNormal;");
    } else {
      fragBuilder->codeAppend("  vec2 surfaceNormal = edgeLightNormal;");
    }
    fragBuilder->codeAppend("  vec2 lightDir = vec2(lightDirX, lightDirY);");
    fragBuilder->codeAppend("  float NdotL = dot(surfaceNormal, lightDir);");
    fragBuilder->codeAppend(
        "  float diffuse = smoothstep(0.35, 1.0, NdotL) * edgeWeight * lightIntensity;");
    fragBuilder->codeAppend(
        "  float rim = smoothstep(0.35, 1.0, -NdotL) * edgeWeight * lightIntensity * 0.6;");
    fragBuilder->codeAppend("  finalColor += vec3(diffuse + rim);");
    fragBuilder->codeAppend("}");
  }

  fragBuilder->codeAppendf("%s = vec4(finalColor, srcAlpha);", args.outputColor.c_str());
}

void GLSLGlassRefractionFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                     UniformData* fragmentUniformData) const {
  auto sourceWidth = static_cast<float>(sourceProxy->width());
  auto sourceHeight = static_cast<float>(sourceProxy->height());

  // GlassP0: glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
  float glassOffsetX =
      (params.glassWidth < sourceWidth) ? (1.0f - params.glassWidth / sourceWidth) * 0.5f : 0.0f;
  float glassOffsetY = (params.glassHeight < sourceHeight)
                           ? (1.0f - params.glassHeight / sourceHeight) * 0.5f
                           : 0.0f;
  float glassScaleX = (params.glassWidth > 0.0f) ? sourceWidth / params.glassWidth : 1.0f;
  float glassScaleY = (params.glassHeight > 0.0f) ? sourceHeight / params.glassHeight : 1.0f;
  float uvTransformData[4] = {glassOffsetX, glassOffsetY, glassScaleX, glassScaleY};
  fragmentUniformData->setData("GlassP0", uvTransformData);

  // GlassP1: halfW, halfH, cornerRadius, minHalf
  float shapeParamsData[4] = {params.halfW, params.halfH, params.cornerRadius, params.minHalf};
  fragmentUniformData->setData("GlassP1", shapeParamsData);

  float lightAngleRad = params.lightAngle * static_cast<float>(M_PI) / 180.0f;
  float lightDirX = std::sin(lightAngleRad);
  float lightDirY = std::cos(lightAngleRad);

  // GlassP2: invOrigW, invOrigH, lightDirX, glassThickness
  float invOrigW = (params.origWidth > 0.0f) ? 1.0f / params.origWidth : 1.0f / sourceWidth;
  float invOrigH = (params.origHeight > 0.0f) ? 1.0f / params.origHeight : 1.0f / sourceHeight;
  float glassThicknessData[4] = {invOrigW, invOrigH, lightDirX, params.glassThickness};
  fragmentUniformData->setData("GlassP2", glassThicknessData);

  // GlassP3: refractionFactor, dispersion, invSourceW, invSourceH
  float refractionParamsData[4] = {params.refractionFactor, params.dispersion, 1.0f / sourceWidth,
                                   1.0f / sourceHeight};
  fragmentUniformData->setData("GlassP3", refractionParamsData);

  // GlassP4: splay, depthRatio, lightDirY, lightIntensity
  float lightParamsData[4] = {params.splay, params.depthRatio, lightDirY, params.lightIntensity};
  fragmentUniformData->setData("GlassP4", lightParamsData);

  // GlassP5: origMinHalf, udfPixelToLayerPixel, renderOffsetX, renderOffsetY
  float miscParamsData[4] = {params.origMinHalf, params.udfPixelToLayerPixel, params.renderOffsetX,
                             params.renderOffsetY};
  fragmentUniformData->setData("GlassP5", miscParamsData);
}

}  // namespace tgfx
