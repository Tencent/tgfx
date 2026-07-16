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

PlacementPtr<FragmentProcessor> GlassRefractionFragmentProcessor::Make(
    BlockAllocator* allocator, PlacementPtr<FragmentProcessor> source,
    PlacementPtr<FragmentProcessor> mask, const GlassRefractionParams& params) {
  if (source == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassRefractionFragmentProcessor>(std::move(source),
                                                                std::move(mask), params);
}

GLSLGlassRefractionFragmentProcessor::GLSLGlassRefractionFragmentProcessor(
    PlacementPtr<FragmentProcessor> source, PlacementPtr<FragmentProcessor> mask,
    const GlassRefractionParams& params)
    : GlassRefractionFragmentProcessor(std::move(source), std::move(mask), params) {
}

void GLSLGlassRefractionFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  auto p0 = uniformHandler->addUniform("GlassP0", UniformFormat::Float4, ShaderStage::Fragment);
  auto p1 = uniformHandler->addUniform("GlassP1", UniformFormat::Float4, ShaderStage::Fragment);
  auto p2 = uniformHandler->addUniform("GlassP2", UniformFormat::Float4, ShaderStage::Fragment);
  auto p3 = uniformHandler->addUniform("GlassP3", UniformFormat::Float4, ShaderStage::Fragment);
  auto p4 = uniformHandler->addUniform("GlassP4", UniformFormat::Float4, ShaderStage::Fragment);

  bool useAxisMix = (params.shapeType == GlassShapeType::RoundedRect ||
                     params.shapeType == GlassShapeType::Ellipse);
  bool useAlphaMask = (params.shapeType == GlassShapeType::Star ||
                       params.shapeType == GlassShapeType::AlphaMask);

  // Emit SDF helper functions for analytical shapes.
  if (params.shapeType == GlassShapeType::RoundedRect) {
    fragBuilder->codeAppendf(R"(
        float glass_outerSDF(float px, float py) {
            float hw = %s.x; float hh = %s.y; float r = %s.z;
            float qx = abs(px) - hw + r; float qy = abs(py) - hh + r;
            float od = sqrt(max(qx,0.0)*max(qx,0.0)+max(qy,0.0)*max(qy,0.0));
            return od + min(max(qx,qy),0.0) - r;
        }
        float glass_innerSDF(float px, float py) {
            float hw = %s.x; float hh = %s.y; float r = %s.z;
            float qx = abs(px) - hw + r; float qy = abs(py) - hh + r;
            float od = sqrt(max(qx,0.0)*max(qx,0.0)+max(qy,0.0)*max(qy,0.0));
            return od + min(max(qx,qy),0.0) - r;
        }
    )", p1.c_str(), p1.c_str(), p1.c_str(), p2.c_str(), p2.c_str(), p2.c_str());
  } else if (params.shapeType == GlassShapeType::Ellipse) {
    fragBuilder->codeAppendf(R"(
        float glass_outerSDF(float px, float py) {
            float hw = %s.x; float hh = %s.y;
            return (length(vec2(px/hw, py/hh)) - 1.0) * min(hw, hh);
        }
        float glass_innerSDF(float px, float py) {
            float hw = %s.x; float hh = %s.y;
            return (length(vec2(px/hw, py/hh)) - 1.0) * min(hw, hh);
        }
    )", p1.c_str(), p1.c_str(), p2.c_str(), p2.c_str());
  }

  // For AlphaMask shapes, sample the mask child (child 1) at custom UVs.
  // Each emitChild call generates a separate texture lookup.
  bool hasMaskChild = (numChildProcessors() > 1);

  // Compute glass pixel coordinates from the varying UV.
  fragBuilder->codeAppendf("vec2 glassUV = %s;", args.fragBuilder->getProgramInfo()
      ? "vTexCoord0" : "vec2(0.0)");
  fragBuilder->codeAppendf("float halfW = %s.x;", p1.c_str());
  fragBuilder->codeAppendf("float halfH = %s.y;", p1.c_str());
  fragBuilder->codeAppendf("float minHalf = %s.w;", p1.c_str());
  fragBuilder->codeAppendf("float glassThickness = %s.w;", p2.c_str());
  fragBuilder->codeAppendf("float refractionFactor = %s.x;", p3.c_str());
  fragBuilder->codeAppendf("float invSourceW = %s.z;", p3.c_str());
  fragBuilder->codeAppendf("float invSourceH = %s.w;", p3.c_str());
  fragBuilder->codeAppendf("float splay = %s.x;", p4.c_str());
  fragBuilder->codeAppendf("float depthRatio = %s.y;", p4.c_str());
  fragBuilder->codeAppendf("float lightAngleRad = %s.z;", p4.c_str());
  fragBuilder->codeAppendf("float lightIntensity = %s.w;", p4.c_str());

  // Sample source at default UV to get the base color.
  std::string sourceColor = "glass_sourceColor";
  emitChild(0, "", &sourceColor, args);

  fragBuilder->codeAppend("vec2 uvOffset = vec2(0.0);");
  fragBuilder->codeAppend("float edgeWeight = 0.0;");
  fragBuilder->codeAppend("vec2 glassNormal = vec2(0.0);");

  // Compute refraction offset based on shape type.
  if (useAxisMix) {
    fragBuilder->codeAppend("float px = (glassUV.x - 0.5) * halfW * 2.0;");
    fragBuilder->codeAppend("float py = (0.5 - glassUV.y) * halfH * 2.0;");
    fragBuilder->codeAppend("float outerSDF = glass_outerSDF(px, py);");
    fragBuilder->codeAppend("float innerSDF = glass_innerSDF(px, py);");
    fragBuilder->codeAppend("if (outerSDF < 0.0) {");
    fragBuilder->codeAppend("float edgeDist = -outerSDF;");
    fragBuilder->codeAppend("edgeWeight = 1.0 - smoothstep(0.0, minHalf * 0.01, edgeDist);");
    fragBuilder->codeAppend("if (innerSDF >= 0.0) {");
    fragBuilder->codeAppend("float totalDist = edgeDist + innerSDF;");
    fragBuilder->codeAppend("float xNorm = (totalDist > 0.001) ? edgeDist / totalDist : 0.0;");
    fragBuilder->codeAppend("xNorm = min(xNorm, 1.0);");
    fragBuilder->codeAppend("float edgeFactor = 1.0 - xNorm;");
    fragBuilder->codeAppend("float offsetDist = glassThickness * refractionFactor * edgeFactor * edgeFactor * 2.0;");
    fragBuilder->codeAppend("float dirLen = sqrt(px * px + py * py);");
    fragBuilder->codeAppend("if (dirLen > 0.001) {");
    fragBuilder->codeAppend("vec2 radialDir = vec2(-px / dirLen, -py / dirLen);");
    fragBuilder->codeAppend("vec2 axisDir = (abs(radialDir.x) > abs(radialDir.y)) ? vec2(sign(radialDir.x), 0.0) : vec2(0.0, sign(radialDir.y));");
    fragBuilder->codeAppend("float maxAxisDot = max(abs(radialDir.x), abs(radialDir.y));");
    fragBuilder->codeAppend("float axisThreshold = mix(0.9, 1.0, depthRatio * splay);");
    fragBuilder->codeAppend("float axisWeight = smoothstep(axisThreshold, 1.0, maxAxisDot);");
    fragBuilder->codeAppend("vec2 refractDir = mix(radialDir, axisDir, axisWeight);");
    fragBuilder->codeAppend("glassNormal = -refractDir;");
    fragBuilder->codeAppend("uvOffset = vec2(refractDir.x * offsetDist * invSourceW, -refractDir.y * offsetDist * invSourceH);");
    fragBuilder->codeAppend("}}");
    fragBuilder->codeAppend("}");
  } else if (useAlphaMask && hasMaskChild) {
    // For AlphaMask, sample the mask child at multiple positions for gradient computation.
    fragBuilder->codeAppend("float px = (glassUV.x - 0.5) * halfW * 2.0;");
    fragBuilder->codeAppend("float py = (0.5 - glassUV.y) * halfH * 2.0;");
    fragBuilder->codeAppend("float step = minHalf / 20.0 + 1.0;");

    // Sample mask at center, right, and top for gradient.
    std::string height0 = "glass_h0";
    std::string height1 = "glass_h1";
    std::string height2 = "glass_h2";
    emitChild(1, "", &height0, args, [](std::string_view) {
      return "vec2((px + halfW) / (halfW * 2.0), 1.0 - (py + halfH) / (halfH * 2.0))";
    });
    emitChild(1, "", &height1, args, [](std::string_view) {
      return "vec2((px + step + halfW) / (halfW * 2.0), 1.0 - (py + halfH) / (halfH * 2.0))";
    });
    emitChild(1, "", &height2, args, [](std::string_view) {
      return "vec2((px + halfW) / (halfW * 2.0), 1.0 - (py + step + halfH) / (halfH * 2.0))";
    });

    fragBuilder->codeAppendf("float height = %s.a;", height0.c_str());
    fragBuilder->codeAppendf("float outerSDF = 0.01 - height;");
    fragBuilder->codeAppend("if (outerSDF < 0.0) {");
    fragBuilder->codeAppendf("float mr = %s.a;", height1.c_str());
    fragBuilder->codeAppendf("float tc = %s.a;", height2.c_str());
    fragBuilder->codeAppend("vec2 grad = vec2(mr - height, tc - height) / step;");
    fragBuilder->codeAppend("float gradLen = length(grad);");
    fragBuilder->codeAppend("if (gradLen > 0.0001) {");
    fragBuilder->codeAppend("vec2 gradDir = grad / gradLen;");
    fragBuilder->codeAppend("float dirLen = sqrt(px * px + py * py);");
    fragBuilder->codeAppend("vec2 centerDir = (dirLen > 0.001) ? vec2(-px / dirLen, -py / dirLen) : vec2(0.0);");
    fragBuilder->codeAppend("vec2 mixedDir = normalize(mix(gradDir, centerDir, splay));");
    fragBuilder->codeAppend("glassNormal = -mixedDir;");
    fragBuilder->codeAppend("float refDist = halfW * (0.5 * refractionFactor + 0.5 * depthRatio);");
    fragBuilder->codeAppend("float offsetDist = refDist * (1.0 - height);");
    fragBuilder->codeAppend("vec2 sn = clamp(mixedDir * offsetDist, -vec2(0.999) * refDist, vec2(0.999) * refDist);");
    fragBuilder->codeAppend("uvOffset = vec2(sn.x * invSourceW, -sn.y * invSourceH);");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("float theta = clamp(0.15 * (0.5 / max(depthRatio, 0.01)), 0.001, 0.15);");
    fragBuilder->codeAppend("edgeWeight = 1.0 - smoothstep(0.5, 0.5 + theta, height);");
    fragBuilder->codeAppend("}");
  }

  // Sample source at displaced UV for the refracted color.
  std::string refractedColor = "glass_refractedColor";
  emitChild(0, "", &refractedColor, args, [](std::string_view coord) {
    return "clamp(" + std::string(coord) + " + uvOffset, vec2(0.0), vec2(1.0))";
  });

  // Apply edge lighting.
  fragBuilder->codeAppendf("vec3 finalColor = %s.rgb;", refractedColor.c_str());
  fragBuilder->codeAppendf("float alpha = %s.a;", refractedColor.c_str());
  fragBuilder->codeAppend("if (lightIntensity > 0.0 && edgeWeight > 0.0) {");
  fragBuilder->codeAppend("vec2 lightDir = vec2(sin(lightAngleRad), cos(lightAngleRad));");
  fragBuilder->codeAppend("float NdotL = dot(glassNormal, lightDir);");
  fragBuilder->codeAppend("finalColor += vec3(smoothstep(0.35, 1.0, NdotL) * edgeWeight * lightIntensity + smoothstep(0.35, 1.0, -NdotL) * edgeWeight * lightIntensity * 0.6);");
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppendf("%s = vec4(finalColor, alpha);", args.outputColor.c_str());
}

void GLSLGlassRefractionFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  float lightAngleRad = params.lightAngle * 3.14159265358979323846f / 180.0f;
  fragmentUniformData->setData("GlassP0",
                               std::array<float, 4>{params.glassOffsetX, params.glassOffsetY,
                                                    params.glassScaleX, params.glassScaleY});
  fragmentUniformData->setData("GlassP1",
                               std::array<float, 4>{params.halfWidth, params.halfHeight,
                                                    params.cornerRadius, params.minHalf});
  fragmentUniformData->setData("GlassP2",
                               std::array<float, 4>{params.innerHalfWidth, params.innerHalfHeight,
                                                    params.innerRadius, params.glassThickness});
  fragmentUniformData->setData("GlassP3",
                               std::array<float, 4>{params.refractionFactor, params.dispersion,
                                                    params.invSourceW, params.invSourceH});
  fragmentUniformData->setData("GlassP4",
                               std::array<float, 4>{params.splay, params.depthRatio,
                                                    lightAngleRad, params.lightIntensity});
}

}  // namespace tgfx
