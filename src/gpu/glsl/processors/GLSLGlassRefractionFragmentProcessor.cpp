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

  // Register uniforms: 6 x vec4, same layout as old GlassUniforms.
  auto p0 = uniformHandler->addUniform("GlassP0", UniformFormat::Float4, ShaderStage::Fragment);
  auto p1 = uniformHandler->addUniform("GlassP1", UniformFormat::Float4, ShaderStage::Fragment);
  auto p2 = uniformHandler->addUniform("GlassP2", UniformFormat::Float4, ShaderStage::Fragment);
  auto p3 = uniformHandler->addUniform("GlassP3", UniformFormat::Float4, ShaderStage::Fragment);
  auto p4 = uniformHandler->addUniform("GlassP4", UniformFormat::Float4, ShaderStage::Fragment);
  auto p5 = uniformHandler->addUniform("GlassP5", UniformFormat::Float4, ShaderStage::Fragment);

  // Get texture samplers.
  auto& sourceSampler = (*args.textureSamplers)[0];

  // Get pixel coordinate from CoordTransform (Identity, no textureProxy → pixel coords).
  auto texCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);

  // --- Emit SDF helper functions via addFunction (placed before main) ---
  std::string outerFn, innerFn;
  if (useAxisMix) {
    if (params.shapeType == GlassShapeType::RoundedRect) {
      outerFn = fragBuilder->getMangledFunctionName("glass_outerSDF");
      innerFn = fragBuilder->getMangledFunctionName("glass_innerSDF");

      fragBuilder->addFunction(
          "float " + outerFn +
          "(float px, float py, float hw, float hh, float r) {\n"
          "  float qx = abs(px) - hw + r;\n"
          "  float qy = abs(py) - hh + r;\n"
          "  float od = sqrt(max(qx,0.0)*max(qx,0.0)+max(qy,0.0)*max(qy,0.0));\n"
          "  return od + min(max(qx,qy),0.0) - r;\n"
          "}\n");

      fragBuilder->addFunction(
          "float " + innerFn +
          "(float px, float py, float ihw, float ihh, float ir) {\n"
          "  float qx = abs(px) - ihw + ir;\n"
          "  float qy = abs(py) - ihh + ir;\n"
          "  float od = sqrt(max(qx,0.0)*max(qx,0.0)+max(qy,0.0)*max(qy,0.0));\n"
          "  return od + min(max(qx,qy),0.0) - ir;\n"
          "}\n");
    } else {
      // Ellipse
      outerFn = fragBuilder->getMangledFunctionName("glass_outerSDF");
      innerFn = fragBuilder->getMangledFunctionName("glass_innerSDF");

      fragBuilder->addFunction("float " + outerFn +
                               "(float px, float py, float hw, float hh) {\n"
                               "  return (length(vec2(px/hw, py/hh)) - 1.0) * min(hw, hh);\n"
                               "}\n");

      fragBuilder->addFunction("float " + innerFn +
                               "(float px, float py, float ihw, float ihh) {\n"
                               "  return (length(vec2(px/ihw, py/ihh)) - 1.0) * min(ihw, ihh);\n"
                               "}\n");
    }
  }

  // --- Main shader body: coordinate conversion first ---
  fragBuilder->codeAppendf("float halfW = %s.x;", p1.c_str());
  fragBuilder->codeAppendf("float halfH = %s.y;", p1.c_str());
  fragBuilder->codeAppendf("float invSourceW = %s.z;", p3.c_str());
  fragBuilder->codeAppendf("float invSourceH = %s.w;", p3.c_str());
  fragBuilder->codeAppendf("vec2 sourceUV = %s * vec2(invSourceW, invSourceH);",
                           texCoordName.c_str());

  // Convert source UV to glass pixel coords centered at origin.
  fragBuilder->codeAppendf("vec2 glassUV = (sourceUV - %s.xy) * %s.zw;", p0.c_str(), p0.c_str());
  fragBuilder->codeAppend("glassUV = vec2(glassUV.x, 1.0 - glassUV.y);");
  fragBuilder->codeAppend("vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);");
  fragBuilder->codeAppend("float px = glassPixel.x - halfW;");
  fragBuilder->codeAppend("float py = glassPixel.y - halfH;");

  // Evaluate SDF after px/py are available.
  if (useAxisMix) {
    if (params.shapeType == GlassShapeType::RoundedRect) {
      fragBuilder->codeAppendf("float cornerRadius = %s.z;", p1.c_str());
      fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH, cornerRadius);",
                               outerFn.c_str());
      fragBuilder->codeAppendf("float innerSDF = %s(px, py, %s.x, %s.y, %s.z);", innerFn.c_str(),
                               p2.c_str(), p2.c_str(), p2.c_str());
    } else {
      fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH);", outerFn.c_str());
      fragBuilder->codeAppendf("float innerSDF = %s(px, py, %s.x, %s.y);", innerFn.c_str(),
                               p2.c_str(), p2.c_str());
    }
  }

  fragBuilder->codeAppend("vec2 uvOffset = vec2(0.0);");

  // Extract common params.
  fragBuilder->codeAppendf("float minHalf = %s.w;", p1.c_str());
  fragBuilder->codeAppendf("float glassThickness = %s.w;", p2.c_str());
  fragBuilder->codeAppendf("float refractionFactor = %s.x;", p3.c_str());
  fragBuilder->codeAppendf("float dispersion = %s.y;", p3.c_str());
  fragBuilder->codeAppendf("float splay = %s.x;", p4.c_str());
  fragBuilder->codeAppendf("float depthRatio = %s.y;", p4.c_str());
  fragBuilder->codeAppendf("float lightAngleRad = %s.z;", p4.c_str());
  fragBuilder->codeAppendf("float lightIntensity = %s.w;", p4.c_str());
  fragBuilder->codeAppendf("float origMinHalf = %s.x;", p5.c_str());
  fragBuilder->codeAppendf("float udfPixelToLayerPixel = %s.y;", p5.c_str());

  // Edge weight and normals.
  fragBuilder->codeAppend("float edgeWeight = 0.0;");
  fragBuilder->codeAppend("vec2 glassNormal = vec2(0.0);");
  fragBuilder->codeAppend("vec2 edgeLightNormal = vec2(0.0);");

  if (useAxisMix) {
    // Analytical SDF refraction (GLASS_USE_AXIS_MIX path).
    fragBuilder->codeAppend("if (outerSDF < 0.0) {");
    fragBuilder->codeAppend("  float edgeDist = -outerSDF;");
    fragBuilder->codeAppend("  edgeWeight = 1.0 - smoothstep(0.0, origMinHalf * 0.01, edgeDist);");
    fragBuilder->codeAppend("  if (innerSDF >= 0.0) {");
    fragBuilder->codeAppend("    float totalDist = edgeDist + innerSDF;");
    fragBuilder->codeAppend("    float xNorm = (totalDist > 0.001) ? edgeDist / totalDist : 0.0;");
    fragBuilder->codeAppend("    xNorm = min(xNorm, 1.0);");
    fragBuilder->codeAppend("    float edgeFactor = 1.0 - xNorm;");
    fragBuilder->codeAppend(
        "    float offsetDist = glassThickness * refractionFactor * edgeFactor * edgeFactor * "
        "2.0;");
    fragBuilder->codeAppend("    float dirLen = sqrt(px * px + py * py);");
    fragBuilder->codeAppend("    if (dirLen > 0.001) {");
    fragBuilder->codeAppend("      vec2 radialDir = vec2(-px / dirLen, -py / dirLen);");
    fragBuilder->codeAppend("      vec2 axisDir;");
    fragBuilder->codeAppend(
        "      if (abs(radialDir.x) > abs(radialDir.y)) { axisDir = vec2(sign(radialDir.x), "
        "0.0); }");
    fragBuilder->codeAppend("      else { axisDir = vec2(0.0, sign(radialDir.y)); }");
    fragBuilder->codeAppend("      float maxAxisDot = max(abs(radialDir.x), abs(radialDir.y));");
    fragBuilder->codeAppend("      float controlValue = depthRatio * splay;");
    fragBuilder->codeAppend("      float axisThreshold = mix(0.9, 1.0, controlValue);");
    fragBuilder->codeAppend("      float axisWeight = smoothstep(axisThreshold, 1.0, maxAxisDot);");
    fragBuilder->codeAppend("      vec2 refractDir = mix(radialDir, axisDir, axisWeight);");
    fragBuilder->codeAppend("      glassNormal = -refractDir;");
    fragBuilder->codeAppend("      float dispX = refractDir.x * offsetDist;");
    fragBuilder->codeAppend("      float dispY = refractDir.y * offsetDist;");
    fragBuilder->codeAppend("      uvOffset = vec2(dispX * invSourceW, -dispY * invSourceH);");
    fragBuilder->codeAppend("    }");
    fragBuilder->codeAppend("  }");
    fragBuilder->codeAppend("}");
  } else if (hasMask) {
    // AlphaMask UDF refraction path.
    auto& maskSampler = (*args.textureSamplers)[1];

    // Inline sampleHeight: compute mask UV, sample, unpack RGBA8.
    // maskUV = vec2((px + halfW) / (halfW * 2.0), 1.0 - (py + halfH) / (halfH * 2.0))
    fragBuilder->codeAppend(
        "vec2 maskUV = vec2((px + halfW) / (halfW * 2.0), "
        "1.0 - (py + halfH) / (halfH * 2.0));");
    fragBuilder->codeAppend("vec4 packedH = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "float height = dot(packedH, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));");

    // Forward difference gradient.
    fragBuilder->codeAppend("float step = (depthRatio * 3.0 + 1.0) * udfPixelToLayerPixel;");
    fragBuilder->codeAppend(
        "vec2 maskUV_mr = vec2((px + step + halfW) / (halfW * 2.0), "
        "1.0 - (py + halfH) / (halfH * 2.0));");
    fragBuilder->codeAppend("vec4 packedMr = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV_mr");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "float mr = dot(packedMr, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));");

    fragBuilder->codeAppend(
        "vec2 maskUV_tc = vec2((px + halfW) / (halfW * 2.0), "
        "1.0 - (py + step + halfH) / (halfH * 2.0));");
    fragBuilder->codeAppend("vec4 packedTc = ");
    fragBuilder->appendTextureLookup(maskSampler, "maskUV_tc");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "float tc = dot(packedTc, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));");

    fragBuilder->codeAppend("vec2 grad = vec2(mr - height, tc - height) / step;");
    fragBuilder->codeAppend("float gradLen = length(grad);");

    // Edge light UDF sampling.
    if (hasCoarseMask) {
      auto& coarseSampler = (*args.textureSamplers)[2];
      fragBuilder->codeAppend("vec4 packedEL = ");
      fragBuilder->appendTextureLookup(coarseSampler, "maskUV");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend(
          "float edgeLightHeight = dot(packedEL, "
          "vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));");
      fragBuilder->codeAppend("edgeWeight = 1.0 - smoothstep(0.5, 0.75, edgeLightHeight);");

      // Edge light normal from coarse UDF gradient.
      fragBuilder->codeAppend("if (edgeWeight > 0.0) {");
      fragBuilder->codeAppend(
          "  vec2 elUV_mr = vec2((px + step + halfW) / (halfW * 2.0), "
          "1.0 - (py + halfH) / (halfH * 2.0));");
      fragBuilder->codeAppend("  vec4 elPackedMr = ");
      fragBuilder->appendTextureLookup(coarseSampler, "elUV_mr");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend(
          "  float elMr = dot(elPackedMr, "
          "vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));");

      fragBuilder->codeAppend(
          "  vec2 elUV_tc = vec2((px + halfW) / (halfW * 2.0), "
          "1.0 - (py + step + halfH) / (halfH * 2.0));");
      fragBuilder->codeAppend("  vec4 elPackedTc = ");
      fragBuilder->appendTextureLookup(coarseSampler, "elUV_tc");
      fragBuilder->codeAppend(";");
      fragBuilder->codeAppend(
          "  float elTc = dot(elPackedTc, "
          "vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));");

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
    fragBuilder->codeAppend("  uvOffset = vec2(sn.x * invSourceW, -sn.y * invSourceH);");
    fragBuilder->codeAppend("}");
  }

  // Dispersion and source sampling.
  fragBuilder->codeAppend("vec2 uvR, uvG, uvB;");
  fragBuilder->codeAppend("if (dispersion < 0.01) {");
  fragBuilder->codeAppend("  uvR = uvG = uvB = clamp(sourceUV + uvOffset, vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend(
      "  uvR = clamp(sourceUV + uvOffset * (1.0 + dispersion), vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend("  uvG = clamp(sourceUV + uvOffset, vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend(
      "  uvB = clamp(sourceUV + uvOffset * (1.0 - dispersion), vec2(0.0), vec2(1.0));");
  fragBuilder->codeAppend("}");

  // Sample source texture for each channel.
  fragBuilder->codeAppend("float rCh = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvR");
  fragBuilder->codeAppend(".r;");

  fragBuilder->codeAppend("float gCh = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvG");
  fragBuilder->codeAppend(".g;");

  fragBuilder->codeAppend("float bCh = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvB");
  fragBuilder->codeAppend(".b;");

  fragBuilder->codeAppend("float aCh = ");
  fragBuilder->appendTextureLookup(sourceSampler, "uvG");
  fragBuilder->codeAppend(".a;");

  fragBuilder->codeAppend("vec3 finalColor = vec3(rCh, gCh, bCh);");

  // Edge lighting.
  fragBuilder->codeAppend("if (lightIntensity > 0.0 && edgeWeight > 0.0) {");
  if (useAxisMix) {
    // For analytical shapes, edgeLightNormal == glassNormal (from shapeNormal).
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
  float p0[4] = {glassOffsetX, glassOffsetY, glassScaleX, glassScaleY};
  fragmentUniformData->setData("GlassP0", p0);

  // GlassP1: halfW, halfH, cornerRadius, minHalf
  float p1[4] = {params.halfW, params.halfH, params.cornerRadius, params.minHalf};
  fragmentUniformData->setData("GlassP1", p1);

  // GlassP2: innerHalfW, innerHalfH, innerRadius, glassThickness
  float p2[4] = {params.innerHalfW, params.innerHalfH, params.innerRadius, params.glassThickness};
  fragmentUniformData->setData("GlassP2", p2);

  // GlassP3: refractionFactor, dispersion, invSourceW, invSourceH
  float p3[4] = {params.refractionFactor, params.dispersion, 1.0f / sourceWidth,
                 1.0f / sourceHeight};
  fragmentUniformData->setData("GlassP3", p3);

  // GlassP4: splay, depthRatio, lightAngleRad, lightIntensity
  float lightAngleRad = params.lightAngle * static_cast<float>(M_PI) / 180.0f;
  float p4[4] = {params.splay, params.depthRatio, lightAngleRad, params.lightIntensity};
  fragmentUniformData->setData("GlassP4", p4);

  // GlassP5: origMinHalf, udfPixelToLayerPixel, 0, 0
  float p5[4] = {params.origMinHalf, params.udfPixelToLayerPixel, 0.0f, 0.0f};
  fragmentUniformData->setData("GlassP5", p5);
}

}  // namespace tgfx
