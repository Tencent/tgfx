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

#include "GLSLGlassShapeGeometryFragmentProcessor.h"

namespace tgfx {
namespace {

void SetGeometryUniforms(UniformData* fragmentUniformData, const GlassShapeGeometryParams& params,
                         float sourceWidth, float sourceHeight) {
  float offsetX =
      params.glassWidth < sourceWidth ? (1.0f - params.glassWidth / sourceWidth) * 0.5f : 0.0f;
  float offsetY =
      params.glassHeight < sourceHeight ? (1.0f - params.glassHeight / sourceHeight) * 0.5f : 0.0f;
  float scaleX = params.glassWidth > 0.0f ? sourceWidth / params.glassWidth : 1.0f;
  float scaleY = params.glassHeight > 0.0f ? sourceHeight / params.glassHeight : 1.0f;
  float transformData[4] = {offsetX, offsetY, scaleX, scaleY};
  fragmentUniformData->setData("GlassShapeP0", transformData);

  float shapeData[4] = {params.halfW, params.halfH, params.cornerRadius, params.minHalf};
  fragmentUniformData->setData("GlassShapeP1", shapeData);

  float effectData[4] = {params.glassThickness, params.refractionFactor, params.splay,
                         params.depthRatio};
  fragmentUniformData->setData("GlassShapeP2", effectData);
}

void EmitGeometryCoordinates(FragmentShaderBuilder* fragBuilder, const std::string& inputColor,
                             const std::string& transform, const std::string& shape) {
  fragBuilder->codeAppendf("vec2 sourceUV = %s.xy;", inputColor.c_str());
  fragBuilder->codeAppendf("vec2 glassUV = (sourceUV - %s.xy) * %s.zw;", transform.c_str(),
                           transform.c_str());
  fragBuilder->codeAppend("glassUV = vec2(glassUV.x, 1.0 - glassUV.y);");
  fragBuilder->codeAppendf("float halfW = %s.x;", shape.c_str());
  fragBuilder->codeAppendf("float halfH = %s.y;", shape.c_str());
  fragBuilder->codeAppend("vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);");
  fragBuilder->codeAppend("float px = glassPixel.x - halfW;");
  fragBuilder->codeAppend("float py = glassPixel.y - halfH;");
}

}  // namespace

PlacementPtr<GlassSDFGeometryFragmentProcessor> GlassSDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, GlassShapeType shapeType, const GlassShapeGeometryParams& params,
    float sourceWidth, float sourceHeight) {
  if (allocator == nullptr || shapeType == GlassShapeType::AlphaMask) {
    return nullptr;
  }
  return allocator->make<GLSLGlassSDFGeometryFragmentProcessor>(shapeType, params, sourceWidth,
                                                                sourceHeight);
}

GLSLGlassSDFGeometryFragmentProcessor::GLSLGlassSDFGeometryFragmentProcessor(
    GlassShapeType shapeType, const GlassShapeGeometryParams& params, float sourceWidth,
    float sourceHeight)
    : GlassSDFGeometryFragmentProcessor(shapeType, params, sourceWidth, sourceHeight) {
}

void GLSLGlassSDFGeometryFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto transform =
      args.uniformHandler->addUniform("GlassShapeP0", UniformFormat::Float4, ShaderStage::Fragment);
  auto shape =
      args.uniformHandler->addUniform("GlassShapeP1", UniformFormat::Float4, ShaderStage::Fragment);
  auto effect =
      args.uniformHandler->addUniform("GlassShapeP2", UniformFormat::Float4, ShaderStage::Fragment);
  auto extra =
      args.uniformHandler->addUniform("GlassShapeP3", UniformFormat::Float, ShaderStage::Fragment);

  std::string sdfFunction = fragBuilder->getMangledFunctionName("glassShapeSDF");
  if (shapeType == GlassShapeType::RoundedRect) {
    fragBuilder->addFunction("float " + sdfFunction +
                             "(float px, float py, float hw, float hh, float r) {\n"
                             "  float dx = abs(px) - hw + r;\n"
                             "  float dy = abs(py) - hh + r;\n"
                             "  float outside = length(max(vec2(dx, dy), vec2(0.0)));\n"
                             "  return outside + min(max(dx, dy), 0.0) - r;\n"
                             "}\n");
  } else {
    fragBuilder->addFunction(
        "float " + sdfFunction +
        "(float px, float py, float hw, float hh) {\n"
        "  float normalizedDist = length(vec2(px / hw, py / hh));\n"
        "  float gradientLength = length(vec2(px / (hw * hw), py / (hh * hh)));\n"
        "  if (gradientLength < 0.000001) return -min(hw, hh);\n"
        "  return normalizedDist * (normalizedDist - 1.0) / gradientLength;\n"
        "}\n");
  }

  EmitGeometryCoordinates(fragBuilder, args.inputColor, transform, shape);
  fragBuilder->codeAppendf("float cornerRadius = %s.z;", shape.c_str());
  if (shapeType == GlassShapeType::RoundedRect) {
    fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH, cornerRadius);",
                             sdfFunction.c_str());
  } else {
    fragBuilder->codeAppendf("float outerSDF = %s(px, py, halfW, halfH);", sdfFunction.c_str());
  }
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppend("if (outerSDF < 0.0) {");
  fragBuilder->codeAppend("  float edgeDist = -outerSDF;");
  fragBuilder->codeAppendf("  float edgeBandWidth = min(%s.w * %s, 60.0);", effect.c_str(),
                           extra.c_str());
  fragBuilder->codeAppend("  float edgeWeight = 1.0 - smoothstep(0.0, 5.0, edgeDist);");
  fragBuilder->codeAppend("  float edgeFactor = 1.0 - min(edgeDist / edgeBandWidth, 1.0);");
  fragBuilder->codeAppendf(
      "  float offsetDist = %s.x * %s.y * edgeFactor * edgeFactor * edgeFactor * 1.2;",
      effect.c_str(), effect.c_str());
  fragBuilder->codeAppendf("  float effectiveSplay = %s.z;", effect.c_str());
  if (shapeType == GlassShapeType::Ellipse) {
    fragBuilder->codeAppend(
        "  vec2 sdfGradient = vec2(px / (halfW * halfW), py / (halfH * halfH));");
    fragBuilder->codeAppend("  float gradientLength = length(sdfGradient);");
    fragBuilder->codeAppend("  if (gradientLength > 0.000001) {");
    fragBuilder->codeAppend("    vec2 gradientDir = -sdfGradient / gradientLength;");
  } else {
    fragBuilder->codeAppend("  vec2 absP = abs(vec2(px, py));");
    fragBuilder->codeAppend("  float qx = absP.x - halfW + cornerRadius;");
    fragBuilder->codeAppend("  float qy = absP.y - halfH + cornerRadius;");
    fragBuilder->codeAppend("  if (cornerRadius > 0.0) {");
    fragBuilder->codeAppend("    float straightHalfW = max(halfW - cornerRadius, 0.0001);");
    fragBuilder->codeAppend("    float straightHalfH = max(halfH - cornerRadius, 0.0001);");
    fragBuilder->codeAppend("    float cornerWeightX = smoothstep(0.0, straightHalfW, absP.x);");
    fragBuilder->codeAppend("    float cornerWeightY = smoothstep(0.0, straightHalfH, absP.y);");
    fragBuilder->codeAppend("    float cornerWeight = (qx > 0.0 && qy > 0.0) ? 1.0 :");
    fragBuilder->codeAppend("        ((qx > qy) ? cornerWeightY : cornerWeightX);");
    fragBuilder->codeAppendf("    effectiveSplay = min(cornerWeight + %s.z, 1.0);", effect.c_str());
    fragBuilder->codeAppend("  }");
    fragBuilder->codeAppend("  vec2 grad;");
    fragBuilder->codeAppend("  if (qx > 0.0 && qy > 0.0) {");
    fragBuilder->codeAppend("    grad = normalize(vec2(qx, qy));");
    fragBuilder->codeAppend("  } else {");
    fragBuilder->codeAppend("    grad = (qx > qy) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);");
    fragBuilder->codeAppend("  }");
    fragBuilder->codeAppend("  float gradientLength = length(grad);");
    fragBuilder->codeAppend("  if (gradientLength > 0.000001) {");
    fragBuilder->codeAppend("    vec2 gradientDir = -vec2(sign(px) * grad.x, sign(py) * grad.y);");
  }
  fragBuilder->codeAppend("    float centerDistance = length(vec2(px, py));");
  fragBuilder->codeAppend(
      "    vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : "
      "gradientDir;");
  fragBuilder->codeAppend("    vec2 refractDir = mix(gradientDir, centerDir, effectiveSplay);");
  fragBuilder->codeAppend("    float refractLength = length(refractDir);");
  fragBuilder->codeAppend(
      "    refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;");
  fragBuilder->codeAppendf("    %s = vec4(refractDir, offsetDist, edgeWeight);",
                           args.outputColor.c_str());
  fragBuilder->codeAppend("  }");
  fragBuilder->codeAppend("}");
}

void GLSLGlassSDFGeometryFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  SetGeometryUniforms(fragmentUniformData, params, sourceWidth, sourceHeight);
  fragmentUniformData->setData("GlassShapeP3", params.origMinHalf);
}

PlacementPtr<GlassUDFGeometryFragmentProcessor> GlassUDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> fineMask,
    std::shared_ptr<TextureProxy> coarseMask, const GlassShapeGeometryParams& params,
    float sourceWidth, float sourceHeight, bool enableEdgeLighting) {
  if (allocator == nullptr || fineMask == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassUDFGeometryFragmentProcessor>(
      std::move(fineMask), std::move(coarseMask), params, sourceWidth, sourceHeight,
      enableEdgeLighting);
}

GLSLGlassUDFGeometryFragmentProcessor::GLSLGlassUDFGeometryFragmentProcessor(
    std::shared_ptr<TextureProxy> fineMask, std::shared_ptr<TextureProxy> coarseMask,
    const GlassShapeGeometryParams& params, float sourceWidth, float sourceHeight,
    bool enableEdgeLighting)
    : GlassUDFGeometryFragmentProcessor(std::move(fineMask), std::move(coarseMask), params,
                                        sourceWidth, sourceHeight, enableEdgeLighting) {
}

void GLSLGlassUDFGeometryFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto transform =
      args.uniformHandler->addUniform("GlassShapeP0", UniformFormat::Float4, ShaderStage::Fragment);
  auto shape =
      args.uniformHandler->addUniform("GlassShapeP1", UniformFormat::Float4, ShaderStage::Fragment);
  auto effect =
      args.uniformHandler->addUniform("GlassShapeP2", UniformFormat::Float4, ShaderStage::Fragment);
  auto& fineSampler = (*args.textureSamplers)[0];

  EmitGeometryCoordinates(fragBuilder, args.inputColor, transform, shape);
  fragBuilder->codeAppend("const vec4 UNPACK = vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0);");
  fragBuilder->codeAppend("vec2 maskUV = vec2(glassUV.x, 1.0 - glassUV.y);");
  fragBuilder->codeAppend("vec4 packedHeight = ");
  fragBuilder->appendTextureLookup(fineSampler, "maskUV");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("float height = dot(packedHeight, UNPACK);");
  fragBuilder->codeAppendf("float gradientStep = (%s.w * 3.0 + 1.0) * %s.w;", effect.c_str(),
                           shape.c_str());
  fragBuilder->codeAppend("vec2 gradientUVStep = gradientStep * vec2(0.5 / halfW, 0.5 / halfH);");
  fragBuilder->codeAppend("vec4 packedRight = ");
  fragBuilder->appendTextureLookup(fineSampler, "maskUV + vec2(gradientUVStep.x, 0.0)");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("vec4 packedUp = ");
  fragBuilder->appendTextureLookup(fineSampler, "maskUV - vec2(0.0, gradientUVStep.y)");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("vec2 gradient = vec2(dot(packedRight, UNPACK) - height,");
  fragBuilder->codeAppend("    dot(packedUp, UNPACK) - height) / gradientStep;");
  fragBuilder->codeAppend("float gradientLength = length(gradient);");
  fragBuilder->codeAppend("float edgeWeight = 0.0;");
  if (coarseMaskProxy != nullptr && enableEdgeLighting) {
    auto& coarseSampler = (*args.textureSamplers)[1];
    fragBuilder->codeAppend("vec4 packedEdgeHeight = ");
    fragBuilder->appendTextureLookup(coarseSampler, "maskUV");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend(
        "edgeWeight = 1.0 - smoothstep(0.5, 0.75, dot(packedEdgeHeight, UNPACK));");
  }
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppend("if (gradientLength > 0.000001) {");
  fragBuilder->codeAppend("  vec2 gradientDir = gradient / gradientLength;");
  fragBuilder->codeAppend("  float centerDistance = length(vec2(px, py));");
  fragBuilder->codeAppend(
      "  vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;");
  fragBuilder->codeAppendf("  vec2 refractDir = mix(gradientDir, centerDir, %s.z);",
                           effect.c_str());
  fragBuilder->codeAppend("  float refractLength = length(refractDir);");
  fragBuilder->codeAppend(
      "  refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;");
  fragBuilder->codeAppendf("  float depthScale = smoothstep(0.0, 0.1, %s.w);", effect.c_str());
  fragBuilder->codeAppendf("  float distance = %s.x * %s.y * %s.w * depthScale;", effect.c_str(),
                           effect.c_str(), effect.c_str());
  fragBuilder->codeAppend(
      "  float proximity = (1.0 - height * height) * (1.0 - height * height) * 1.2;");
  fragBuilder->codeAppendf("  %s = vec4(refractDir, distance * proximity, edgeWeight);",
                           args.outputColor.c_str());
  fragBuilder->codeAppend("}");
}

void GLSLGlassUDFGeometryFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  SetGeometryUniforms(fragmentUniformData, params, sourceWidth, sourceHeight);
  float shapeData[4] = {params.halfW, params.halfH, params.cornerRadius,
                        params.udfPixelToLayerPixel};
  fragmentUniformData->setData("GlassShapeP1", shapeData);
  float effectData[4] = {params.minHalf, params.refractionFactor, params.splay, params.depthRatio};
  fragmentUniformData->setData("GlassShapeP2", effectData);
}

}  // namespace tgfx
