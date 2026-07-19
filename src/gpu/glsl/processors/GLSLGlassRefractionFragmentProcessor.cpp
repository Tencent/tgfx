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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLGlassRefractionFragmentProcessor.h"

namespace tgfx {

PlacementPtr<GlassRefractionFragmentProcessor> GlassRefractionFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
    std::shared_ptr<TextureProxy> fineMask, std::shared_ptr<TextureProxy> coarseMask,
    const GlassRefractionParams& params) {
  if (allocator == nullptr || source == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassRefractionFragmentProcessor>(
      std::move(source), std::move(fineMask), std::move(coarseMask), params);
}

GLSLGlassRefractionFragmentProcessor::GLSLGlassRefractionFragmentProcessor(
    std::shared_ptr<TextureProxy> source, std::shared_ptr<TextureProxy> fineMask,
    std::shared_ptr<TextureProxy> coarseMask, const GlassRefractionParams& params)
    : GlassRefractionFragmentProcessor(std::move(source), std::move(fineMask),
                                       std::move(coarseMask), params) {
}

void GLSLGlassRefractionFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;
  const bool hasMask = (fineMaskProxy != nullptr);
  const bool hasCoarseMask = (coarseMaskProxy != nullptr);
  const bool useAxisMix = (params.shapeType == GlassShapeType::RoundedRect ||
                           params.shapeType == GlassShapeType::Ellipse);

  // Register uniforms: 6 x vec4.
  // uvTransform: glassOffsetX, glassOffsetY, glassScaleX, glassScaleY
  auto uvTransform =
      uniformHandler->addUniform("GlassP0", UniformFormat::Float4, ShaderStage::Fragment);
  // shapeParams: halfW, halfH, cornerRadius, minHalf
  auto shapeParams =
      uniformHandler->addUniform("GlassP1", UniformFormat::Float4, ShaderStage::Fragment);
  // glassThicknessParam: invOrigW(.x), invOrigH(.y), unused(.z), glassThickness(.w)
  auto glassThicknessParam =
      uniformHandler->addUniform("GlassP2", UniformFormat::Float4, ShaderStage::Fragment);
  // refractionParams: refractionFactor, dispersion, invSourceW, invSourceH
  auto refractionParams =
      uniformHandler->addUniform("GlassP3", UniformFormat::Float4, ShaderStage::Fragment);
  // lightParams: splay, depthRatio, lightAngleRad, lightIntensity
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
  std::string outerFn;
  if (useAxisMix) {
    if (params.shapeType == GlassShapeType::RoundedRect) {
      outerFn = fragBuilder->getMangledFunctionName("glass_outerSDF");

      fragBuilder->addFunction(
          "float " + outerFn +
          "(float px, float py, float hw, float hh, float r) {\n"
          "  float qx = abs(px) - hw + r;\n"
          "  float qy = abs(py) - hh + r;\n"
          "  float od = sqrt(max(qx,0.0)*max(qx,0.0)+max(qy,0.0)*max(qy,0.0));\n"
          "  return od + min(max(qx,qy),0.0) - r;\n"
          "}\n");
    } else {
      // Ellipse
      outerFn = fragBuilder->getMangledFunctionName("glass_outerSDF");

      fragBuilder->addFunction("float " + outerFn +
                               "(float px, float py, float hw, float hh) {\n"
                               "  float t = length(vec2(px/hw, py/hh));\n"
                               "  float gradLen = length(vec2(px/(hw*hw), py/(hh*hh)));\n"
                               "  if (gradLen < 0.000001) return -min(hw, hh);\n"
                               "  return t * (t - 1.0) / gradLen;\n"
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
                               outerFn.c_str());
    } else {
      fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH);", outerFn.c_str());
    }
  }

  fragBuilder->codeAppend("vec2 uvOffset = vec2(0.0);");

  // Extract common params.
  fragBuilder->codeAppendf("float minHalf = %s.w;", shapeParams.c_str());
  fragBuilder->codeAppendf("float invOrigW = %s.x;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float invOrigH = %s.y;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float glassThickness = %s.w;", glassThicknessParam.c_str());
  fragBuilder->codeAppendf("float refractionFactor = %s.x;", refractionParams.c_str());
  fragBuilder->codeAppendf("float dispersion = %s.y;", refractionParams.c_str());
  fragBuilder->codeAppendf("float splay = %s.x;", lightParams.c_str());
  fragBuilder->codeAppendf("float depthRatio = %s.y;", lightParams.c_str());
  fragBuilder->codeAppendf("float lightAngleRad = %s.z;", lightParams.c_str());
  fragBuilder->codeAppendf("float lightIntensity = %s.w;", lightParams.c_str());
  fragBuilder->codeAppendf("float origMinHalf = %s.x;", miscParams.c_str());
  fragBuilder->codeAppendf("float udfPixelToLayerPixel = %s.y;", miscParams.c_str());

  // Edge weight and normals.
  fragBuilder->codeAppend("float edgeWeight = 0.0;");
  fragBuilder->codeAppend("vec2 glassNormal = vec2(0.0);");
  fragBuilder->codeAppend("vec2 edgeLightNormal = vec2(0.0);");

  if (useAxisMix) {
    // Analytical SDF refraction (GLASS_USE_AXIS_MIX path).
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
      fragBuilder->codeAppend("  vec2 gradDir = vec2(px / (halfW * halfW), py / (halfH * halfH));");
      fragBuilder->codeAppend("  float dirLen = length(gradDir);");
      fragBuilder->codeAppend("  if (dirLen > 0.000001) {");
      fragBuilder->codeAppend("    vec2 radialDir = -gradDir / dirLen;");
    } else {
      // RoundedRect: radial direction from center.
      fragBuilder->codeAppend("  float dirLen = sqrt(px * px + py * py);");
      fragBuilder->codeAppend("  if (dirLen > 0.001) {");
      fragBuilder->codeAppend("    vec2 radialDir = vec2(-px / dirLen, -py / dirLen);");
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
    fragBuilder->codeAppend("    float dispX = refractDir.x * offsetDist;");
    fragBuilder->codeAppend("    float dispY = refractDir.y * offsetDist;");
    fragBuilder->codeAppend("    uvOffset = vec2(dispX * invOrigW, -dispY * invOrigH);");
    fragBuilder->codeAppend("  }");
    fragBuilder->codeAppend("}");
  } else if (hasMask) {
    // AlphaMask UDF refraction path.
    auto& maskSampler = (*args.textureSamplers)[1];

    // RGBA8 unpack constants for decoding packed float from 4 channels.
    fragBuilder->codeAppend(
        "const vec4 UNPACK = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);");

    // Inline sampleHeight: compute mask UV, sample, unpack RGBA8.
    fragBuilder->codeAppend(
        "vec2 maskUV = vec2((px + halfW) / (halfW * 2.0), "
        "1.0 - (py + halfH) / (halfH * 2.0));");
    fragBuilder->codeAppend("vec4 packedH = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("float height = dot(packedH, UNPACK);");

    // Forward difference gradient.
    fragBuilder->codeAppend("float step = (depthRatio * 3.0 + 1.0) * udfPixelToLayerPixel;");
    fragBuilder->codeAppend(
        "vec2 maskUV_mr = vec2((px + step + halfW) / (halfW * 2.0), "
        "1.0 - (py + halfH) / (halfH * 2.0));");
    fragBuilder->codeAppend("vec4 packedMr = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV_mr");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("float mr = dot(packedMr, UNPACK);");

    fragBuilder->codeAppend(
        "vec2 maskUV_tc = vec2((px + halfW) / (halfW * 2.0), "
        "1.0 - (py + step + halfH) / (halfH * 2.0));");
    fragBuilder->codeAppend("vec4 packedTc = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV_tc");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("float tc = dot(packedTc, UNPACK);");

    fragBuilder->codeAppend("vec2 grad = vec2(mr - height, tc - height) / step;");
    fragBuilder->codeAppend("float gradLen = length(grad);");

    // Edge light UDF sampling.
    if (hasCoarseMask) {
      auto& coarseSampler = (*args.textureSamplers)[2];
      fragBuilder->codeAppend("vec4 packedEL = ");
      fragBuilder->appendTextureLookup(coarseSampler, "maskUV");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend("float edgeLightHeight = dot(packedEL, UNPACK);");
      fragBuilder->codeAppend("edgeWeight = 1.0 - smoothstep(0.5, 0.75, edgeLightHeight);");

      // Edge light normal from coarse UDF gradient.
      fragBuilder->codeAppend("if (edgeWeight > 0.0) {");
      fragBuilder->codeAppend(
          "  vec2 elUV_mr = vec2((px + step + halfW) / (halfW * 2.0), "
          "1.0 - (py + halfH) / (halfH * 2.0));");
      fragBuilder->codeAppend("  vec4 elPackedMr = ");
      fragBuilder->appendTextureLookup(coarseSampler, "elUV_mr");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend("  float elMr = dot(elPackedMr, UNPACK);");

      fragBuilder->codeAppend(
          "  vec2 elUV_tc = vec2((px + halfW) / (halfW * 2.0), "
          "1.0 - (py + step + halfH) / (halfH * 2.0));");
      fragBuilder->codeAppend("  vec4 elPackedTc = ");
      fragBuilder->appendTextureLookup(coarseSampler, "elUV_tc");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend("  float elTc = dot(elPackedTc, UNPACK);");

      fragBuilder->codeAppend(
          "  vec2 elGrad = vec2(elMr - edgeLightHeight, elTc - edgeLightHeight) / step;");
      fragBuilder->codeAppend("  float elGradLen = length(elGrad);");
      fragBuilder->codeAppend("  if (elGradLen > 0.000001) {");
      fragBuilder->codeAppend("    vec2 elGradDir = elGrad / elGradLen;");
      fragBuilder->codeAppend("    float dirLen = sqrt(px * px + py * py);");
      fragBuilder->codeAppend(
          "    vec2 centerDir = (dirLen > 0.001) ? vec2(-px / dirLen, -py / dirLen) : vec2(0.0);");
      fragBuilder->codeAppend("    vec2 elMixedDir = mix(elGradDir, centerDir, splay);");
      fragBuilder->codeAppend("    elMixedDir = normalize(elMixedDir);");
      fragBuilder->codeAppend("    edgeLightNormal = -elMixedDir;");
      fragBuilder->codeAppend("  }");
      fragBuilder->codeAppend("}");
    }

    // Refraction from UDF gradient.
    fragBuilder->codeAppend("if (gradLen > 0.000001) {");
    fragBuilder->codeAppend("  vec2 gradDir = grad / gradLen;");
    fragBuilder->codeAppend("  float dirLen = sqrt(px * px + py * py);");
    fragBuilder->codeAppend(
        "  vec2 centerDir = (dirLen > 0.001) ? vec2(-px / dirLen, -py / dirLen) : vec2(0.0);");
    fragBuilder->codeAppend("  vec2 mixedDir = mix(gradDir, centerDir, splay);");
    fragBuilder->codeAppend("  mixedDir = normalize(mixedDir);");
    fragBuilder->codeAppend("  glassNormal = -mixedDir;");
    fragBuilder->codeAppend("  float depthScale = smoothstep(0.0, 0.1, depthRatio);");
    fragBuilder->codeAppend(
        "  float refDist = halfW * refractionFactor * depthRatio * depthScale;");
    fragBuilder->codeAppend("  float edgeProximity = height * (1.0 - height);");
    fragBuilder->codeAppend("  float offsetDist = refDist * edgeProximity;");
    fragBuilder->codeAppend("  vec2 sn = mixedDir * offsetDist;");
    fragBuilder->codeAppend("  vec2 sk = vec2(0.999) * refDist;");
    fragBuilder->codeAppend("  sn = clamp(sn, -sk, sk);");
    fragBuilder->codeAppend("  uvOffset = vec2(sn.x * invOrigW, -sn.y * invOrigH);");
    fragBuilder->codeAppend("}");
  }

  // Dispersion and source sampling.
  fragBuilder->codeAppend("vec3 finalColor;");
  fragBuilder->codeAppend("float aCh;");
  fragBuilder->codeAppend("if (dispersion < 0.01) {");
  fragBuilder->codeAppend("  vec2 uv = clamp(sourceUV + uvOffset, vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend("  vec4 col = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uv");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("  finalColor = col.rgb;");
  fragBuilder->codeAppend("  aCh = col.a;");
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend(
      "  vec2 uvR = clamp(sourceUV + uvOffset * (1.0 + dispersion), vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend("  vec2 uvG = clamp(sourceUV + uvOffset, vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend(
      "  vec2 uvB = clamp(sourceUV + uvOffset * (1.0 - dispersion), vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend("  finalColor.r = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvR");
  fragBuilder->codeAppend(".r;");
  fragBuilder->codeAppend("  finalColor.g = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvG");
  fragBuilder->codeAppend(".g;");
  fragBuilder->codeAppend("  finalColor.b = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvB");
  fragBuilder->codeAppend(".b;");
  fragBuilder->codeAppend("  aCh = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvG");
  fragBuilder->codeAppend(".a;");
  fragBuilder->codeAppend("}");

  // Edge lighting.
  fragBuilder->codeAppend("if (lightIntensity > 0.0 && edgeWeight > 0.0) {");
  if (useAxisMix) {
    fragBuilder->codeAppend("  vec2 N = glassNormal;");
  } else {
    fragBuilder->codeAppend("  vec2 N = edgeLightNormal;");
  }
  fragBuilder->codeAppend("  vec2 lightDir = vec2(sin(lightAngleRad), cos(lightAngleRad));");
  fragBuilder->codeAppend("  float NdotL = dot(N, lightDir);");
  fragBuilder->codeAppend(
      "  float diffuse = smoothstep(0.35, 1.0, NdotL) * edgeWeight * lightIntensity;");
  fragBuilder->codeAppend(
      "  float rim = smoothstep(0.35, 1.0, -NdotL) * edgeWeight * lightIntensity * 0.6;");
  fragBuilder->codeAppend("  finalColor += vec3(diffuse + rim);");
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppendf("%s = vec4(finalColor, aCh);", args.outputColor.c_str());
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

  // GlassP2: invOrigW, invOrigH, unused, glassThickness
  float invOrigW = (params.origWidth > 0.0f) ? 1.0f / params.origWidth : 1.0f / sourceWidth;
  float invOrigH = (params.origHeight > 0.0f) ? 1.0f / params.origHeight : 1.0f / sourceHeight;
  float glassThicknessData[4] = {invOrigW, invOrigH, 0.0f, params.glassThickness};
  fragmentUniformData->setData("GlassP2", glassThicknessData);

  // GlassP3: refractionFactor, dispersion, invSourceW, invSourceH
  float refractionParamsData[4] = {params.refractionFactor, params.dispersion, 1.0f / sourceWidth,
                                   1.0f / sourceHeight};
  fragmentUniformData->setData("GlassP3", refractionParamsData);

  // GlassP4: splay, depthRatio, lightAngleRad, lightIntensity
  float lightAngleRad = params.lightAngle * static_cast<float>(M_PI) / 180.0f;
  float lightParamsData[4] = {params.splay, params.depthRatio, lightAngleRad,
                              params.lightIntensity};
  fragmentUniformData->setData("GlassP4", lightParamsData);

  // GlassP5: origMinHalf, udfPixelToLayerPixel, renderOffsetX, renderOffsetY
  float miscParamsData[4] = {params.origMinHalf, params.udfPixelToLayerPixel, params.renderOffsetX,
                             params.renderOffsetY};
  fragmentUniformData->setData("GlassP5", miscParamsData);
}

}  // namespace tgfx
