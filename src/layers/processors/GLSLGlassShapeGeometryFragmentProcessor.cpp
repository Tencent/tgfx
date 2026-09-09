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
#include <cmath>

namespace tgfx {
namespace {

void EmitGeometryCoordinates(FragmentShaderBuilder* fragBuilder, const std::string& inputColor,
                             const std::string& shape) {
  fragBuilder->codeAppendf("vec2 glassUV = %s.xy;", inputColor.c_str());
  fragBuilder->codeAppend("glassUV = vec2(glassUV.x, 1.0 - glassUV.y);");
  fragBuilder->codeAppendf("float halfW = %s.x;", shape.c_str());
  fragBuilder->codeAppendf("float halfH = %s.y;", shape.c_str());
  fragBuilder->codeAppend("vec2 glassPixel = glassUV * vec2(halfW * 2.0, halfH * 2.0);");
  fragBuilder->codeAppend("float px = glassPixel.x - halfW;");
  fragBuilder->codeAppend("float py = glassPixel.y - halfH;");
}

}  // namespace

PlacementPtr<GlassSDFGeometryFragmentProcessor> GlassSDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, GlassShapeType shapeType, const GlassSDFGeometryParams& params) {
  if (allocator == nullptr || shapeType == GlassShapeType::AlphaMask) {
    return nullptr;
  }
  return allocator->make<GLSLGlassSDFGeometryFragmentProcessor>(shapeType, params);
}

GLSLGlassSDFGeometryFragmentProcessor::GLSLGlassSDFGeometryFragmentProcessor(
    GlassShapeType shapeType, const GlassSDFGeometryParams& params)
    : GlassSDFGeometryFragmentProcessor(shapeType, params) {
}

void GLSLGlassSDFGeometryFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto shape =
      args.uniformHandler->addUniform("GlassShapeP0", UniformFormat::Float4, ShaderStage::Fragment);
  auto effect =
      args.uniformHandler->addUniform("GlassShapeP1", UniformFormat::Float4, ShaderStage::Fragment);

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

  EmitGeometryCoordinates(fragBuilder, args.inputColor, shape);
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
  fragBuilder->codeAppendf("  float edgeBand = max(1.0, %s.w);", effect.c_str());
  fragBuilder->codeAppend("  float edgeWeight = 1.0 - smoothstep(0.0, edgeBand, edgeDist);");
  // Figma profile with rd = glassThickness, bs = 0 (no bevel):
  // offset = I * rd * pow(clamp((rd - d) / rd, 0, 1), 3.5), capped at 0.999 * rd.
  fragBuilder->codeAppendf("  float rd = max(%s.w, 0.0001);", shape.c_str());
  fragBuilder->codeAppend("  float heightT = clamp((rd - edgeDist) / rd, 0.0, 1.0);");
  fragBuilder->codeAppendf("  float offsetDist = min(%s.x * rd * pow(heightT, 3.5), 0.999 * rd);",
                           effect.c_str());
  fragBuilder->codeAppendf("  float effectiveSplay = %s.y;", effect.c_str());
  if (shapeType == GlassShapeType::Ellipse) {
    fragBuilder->codeAppend(
        "  vec2 sdfGradient = vec2(px / (halfW * halfW), py / (halfH * halfH));");
    fragBuilder->codeAppend("  float gradientLength = length(sdfGradient);");
    fragBuilder->codeAppend("  if (gradientLength > 0.000001) {");
    fragBuilder->codeAppend("    vec2 gradientDir = -sdfGradient / gradientLength;");
  } else {
    // Figma-style corner radius amplification: effective corner radius grows with
    // refractionDistance (glassThickness), so larger refraction produces larger corner
    // regions and more radial direction blending at corners.
    fragBuilder->codeAppend("  vec2 absP = abs(vec2(px, py));");
    fragBuilder->codeAppendf("  float refractionDistance = %s.w;", shape.c_str());
    fragBuilder->codeAppend(
        "  float r_effective = min(min(halfW, halfH), max(cornerRadius, refractionDistance));");
    fragBuilder->codeAppend(
        "  vec2 cornerCenter = vec2(halfW - r_effective, halfH - r_effective);");
    fragBuilder->codeAppend("  vec2 relToCorner = absP - cornerCenter;");
    fragBuilder->codeAppend("  float compensation = max(-min(relToCorner.x, relToCorner.y), 0.0);");
    fragBuilder->codeAppend("  vec2 grad = relToCorner + vec2(compensation);");
    fragBuilder->codeAppend("  float qx = absP.x - cornerCenter.x;");
    fragBuilder->codeAppend("  float qy = absP.y - cornerCenter.y;");
    fragBuilder->codeAppend("  float straightHalfW = max(cornerCenter.x, 0.0001);");
    fragBuilder->codeAppend("  float straightHalfH = max(cornerCenter.y, 0.0001);");
    fragBuilder->codeAppend("  float cornerWeightX = smoothstep(0.0, straightHalfW, absP.x);");
    fragBuilder->codeAppend("  float cornerWeightY = smoothstep(0.0, straightHalfH, absP.y);");
    fragBuilder->codeAppend("  float cornerWeight = (qx > 0.0 && qy > 0.0) ? 1.0 :");
    fragBuilder->codeAppend("        ((qx > qy) ? cornerWeightY : cornerWeightX);");
    fragBuilder->codeAppendf("    effectiveSplay = min(cornerWeight + %s.y, 1.0);", effect.c_str());
    fragBuilder->codeAppend("  float gradientLength = length(grad);");
    fragBuilder->codeAppend("  if (gradientLength > 0.000001) {");
    fragBuilder->codeAppend(
        "    vec2 gradientDir = -vec2(sign(px) * grad.x, sign(py) * grad.y) / gradientLength;");
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
  float shapeData[4] = {params.halfW, params.halfH, params.cornerRadius, params.glassThickness};
  fragmentUniformData->setData("GlassShapeP0", shapeData);
  float effectData[4] = {params.refractionFactor, params.splay, params.depthRatio,
                         params.edgeBandLayerPixels};
  fragmentUniformData->setData("GlassShapeP1", effectData);
}

PlacementPtr<GlassUDFGeometryFragmentProcessor> GlassUDFGeometryFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> mask,
    std::shared_ptr<TextureProxy> edgeMask, const GlassUDFGeometryParams& params,
    bool enableEdgeLighting) {
  if (allocator == nullptr || mask == nullptr) {
    return nullptr;
  }
  if (enableEdgeLighting && edgeMask == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLGlassUDFGeometryFragmentProcessor>(
      std::move(mask), std::move(edgeMask), params, enableEdgeLighting);
}

GLSLGlassUDFGeometryFragmentProcessor::GLSLGlassUDFGeometryFragmentProcessor(
    std::shared_ptr<TextureProxy> mask, std::shared_ptr<TextureProxy> edgeMask,
    const GlassUDFGeometryParams& params, bool enableEdgeLighting)
    : GlassUDFGeometryFragmentProcessor(std::move(mask), std::move(edgeMask), params,
                                        enableEdgeLighting) {
}

void GLSLGlassUDFGeometryFragmentProcessor::emitCode(EmitArgs& args) const {
  auto* fragBuilder = args.fragBuilder;
  auto shape =
      args.uniformHandler->addUniform("GlassShapeP0", UniformFormat::Float4, ShaderStage::Fragment);
  auto effect =
      args.uniformHandler->addUniform("GlassShapeP1", UniformFormat::Float4, ShaderStage::Fragment);
  auto fineMaskUV = args.uniformHandler->addUniform("GlassFineMaskUV", UniformFormat::Float4,
                                                    ShaderStage::Fragment);
  auto edgeSpan = args.uniformHandler->addUniform("GlassEdgeSpan", UniformFormat::Float2,
                                                  ShaderStage::Fragment);
  auto edgeMaskUV = args.uniformHandler->addUniform("GlassEdgeMaskUV", UniformFormat::Float4,
                                                    ShaderStage::Fragment);
  auto& maskSampler = (*args.textureSamplers)[0];

  EmitGeometryCoordinates(fragBuilder, args.inputColor, shape);
  // The fine mask packs the refraction height into RGB with 24-bit precision; the edge mask
  // carries the edge light height in A.
  fragBuilder->codeAppend("const vec3 UNPACK24 = vec3(1.0, 1.0/255.0, 1.0/65025.0);");
  fragBuilder->codeAppend("vec2 maskUV = vec2(glassUV.x, 1.0 - glassUV.y);");
  fragBuilder->codeAppendf("vec2 fineUV = maskUV * %s.xy + %s.zw;", fineMaskUV.c_str(),
                           fineMaskUV.c_str());
  fragBuilder->codeAppend("vec4 packedCenter = ");
  fragBuilder->appendTextureLookup(maskSampler, "fineUV");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("float height = dot(packedCenter.rgb, UNPACK24);");
  fragBuilder->codeAppendf("float gradientBase = %s.z * 3.0 + 1.0;", effect.c_str());
  fragBuilder->codeAppendf("vec2 gradientStep = gradientBase * %s.zw;", shape.c_str());
  fragBuilder->codeAppendf(
      "vec2 gradientUVStep = gradientStep * vec2(0.5 / halfW, 0.5 / halfH) * %s.xy;",
      fineMaskUV.c_str());
  fragBuilder->codeAppend("vec4 packedRight = ");
  fragBuilder->appendTextureLookup(maskSampler, "fineUV + vec2(gradientUVStep.x, 0.0)");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("vec4 packedUp = ");
  fragBuilder->appendTextureLookup(maskSampler, "fineUV - vec2(0.0, gradientUVStep.y)");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppend("float deltaRight = dot(packedRight.rgb, UNPACK24) - height;");
  fragBuilder->codeAppend("float deltaUp = dot(packedUp.rgb, UNPACK24) - height;");
  fragBuilder->codeAppend("vec2 gradient = vec2(deltaRight, deltaUp) / gradientStep;");
  fragBuilder->codeAppend("float gradientLength = length(gradient);");
  fragBuilder->codeAppend("float gradientSignal = max(abs(deltaRight), abs(deltaUp));");
  fragBuilder->codeAppend("float gradientWeight = smoothstep(0.001, 0.005, gradientSignal);");
  fragBuilder->codeAppend("float edgeWeight = 0.0;");
  if (enableEdgeLighting) {
    auto& edgeSampler = (*args.textureSamplers)[1];
    fragBuilder->codeAppendf("vec2 edgeUV = maskUV * %s.xy + %s.zw;", edgeMaskUV.c_str(),
                             edgeMaskUV.c_str());
    fragBuilder->codeAppend("float edgeHeight = ");
    fragBuilder->appendTextureLookup(edgeSampler, "edgeUV");
    fragBuilder->codeAppend(".a;");
    // Sample half a span to each side so the center difference spans exactly the tent radius that
    // produced the edge field; dividing the height difference by that same span makes the
    // reconstructed distance independent of the radius.
    fragBuilder->codeAppendf(
        "vec2 edgeUVStep = %s * vec2(0.25 / max(halfW, 0.0001), 0.25 / max(halfH, 0.0001)) * "
        "%s.xy;",
        edgeSpan.c_str(), edgeMaskUV.c_str());
    fragBuilder->codeAppend("vec4 packedEdgeRight = ");
    fragBuilder->appendTextureLookup(edgeSampler, "edgeUV + vec2(edgeUVStep.x, 0.0)");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("vec4 packedEdgeLeft = ");
    fragBuilder->appendTextureLookup(edgeSampler, "edgeUV - vec2(edgeUVStep.x, 0.0)");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("vec4 packedEdgeUp = ");
    fragBuilder->appendTextureLookup(edgeSampler, "edgeUV - vec2(0.0, edgeUVStep.y)");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppend("vec4 packedEdgeDown = ");
    fragBuilder->appendTextureLookup(edgeSampler, "edgeUV + vec2(0.0, edgeUVStep.y)");
    fragBuilder->codeAppend(";");
    fragBuilder->codeAppendf(
        "vec2 edgeGradient = vec2((packedEdgeRight.a - packedEdgeLeft.a) / max(%s.x, 0.0001),"
        " (packedEdgeUp.a - packedEdgeDown.a) / max(%s.y, 0.0001));",
        edgeSpan.c_str(), edgeSpan.c_str());
    fragBuilder->codeAppend("float edgeGradientLength = max(length(edgeGradient), 0.0001);");
    fragBuilder->codeAppend(
        "float edgeDistance = max((edgeHeight - 0.5) / edgeGradientLength, 0.0);");
    // The distance is in layer pixels, so the band is widened by the caller when the layer renders
    // at a reduced scale; without that the falloff collapses into a hard threshold.
    fragBuilder->codeAppendf("float edgeBand = max(1.0, %s.w);", effect.c_str());
    fragBuilder->codeAppend("edgeWeight = 1.0 - smoothstep(0.0, edgeBand, edgeDistance);");
  }
  fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  fragBuilder->codeAppend("if (gradientLength > 0.000001 && gradientWeight > 0.000001) {");
  fragBuilder->codeAppend("  vec2 gradientDir = gradient / gradientLength;");
  fragBuilder->codeAppend("  float centerDistance = length(vec2(px, py));");
  fragBuilder->codeAppend(
      "  vec2 centerDir = centerDistance > 0.001 ? -vec2(px, py) / centerDistance : gradientDir;");
  fragBuilder->codeAppendf("  vec2 refractDir = mix(gradientDir, centerDir, %s.y);",
                           effect.c_str());
  fragBuilder->codeAppend("  float refractLength = length(refractDir);");
  fragBuilder->codeAppend(
      "  refractDir = refractLength < 0.000001 ? gradientDir : refractDir / refractLength;");
  fragBuilder->codeAppendf("  float depthScale = smoothstep(0.0, 0.1, %s.z);", effect.c_str());
  fragBuilder->codeAppendf(
      "  float distance = min(halfW, halfH) * %s.x * %s.z * depthScale * gradientWeight;",
      effect.c_str(), effect.c_str());
  fragBuilder->codeAppend(
      "  float proximity = (1.0 - height * height) * (1.0 - height * height) * 1.2;");
  fragBuilder->codeAppendf("  %s = vec4(refractDir, distance * proximity, edgeWeight);",
                           args.outputColor.c_str());
  fragBuilder->codeAppend("}");
}

void GLSLGlassUDFGeometryFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  float shapeData[4] = {params.halfW, params.halfH, params.udfPixelToLayerPixelX,
                        params.udfPixelToLayerPixelY};
  fragmentUniformData->setData("GlassShapeP0", shapeData);
  float effectData[4] = {params.refractionFactor, params.splay, params.depthRatio,
                         params.edgeBandLayerPixels};
  fragmentUniformData->setData("GlassShapeP1", effectData);
  float edgeSpanData[2] = {params.edgeSpanX, params.edgeSpanY};
  fragmentUniformData->setData("GlassEdgeSpan", edgeSpanData);
  float textureWidth = static_cast<float>(maskProxy->width());
  float textureHeight = static_cast<float>(maskProxy->height());
  float coreWidth = std::round(params.halfW * 2.0f / params.udfPixelToLayerPixelX);
  float coreHeight = std::round(params.halfH * 2.0f / params.udfPixelToLayerPixelY);
  float scaleX = coreWidth / textureWidth;
  float scaleY = coreHeight / textureHeight;
  float maskUVData[4] = {scaleX, scaleY, -params.textureOriginX / textureWidth,
                         -params.textureOriginY / textureHeight};
  fragmentUniformData->setData("GlassFineMaskUV", maskUVData);
  float edgeScaleX = 1.0f;
  float edgeScaleY = 1.0f;
  float edgeOriginX = params.edgeTextureOriginX;
  float edgeOriginY = params.edgeTextureOriginY;
  if (enableEdgeLighting && edgeMaskProxy != nullptr) {
    float edgeTextureWidth = static_cast<float>(edgeMaskProxy->width());
    float edgeTextureHeight = static_cast<float>(edgeMaskProxy->height());
    float edgeCoreWidth = std::round(params.halfW * 2.0f / params.edgePixelToLayerPixelX);
    float edgeCoreHeight = std::round(params.halfH * 2.0f / params.edgePixelToLayerPixelY);
    edgeScaleX = edgeCoreWidth / edgeTextureWidth;
    edgeScaleY = edgeCoreHeight / edgeTextureHeight;
    edgeOriginX = -params.edgeTextureOriginX / edgeTextureWidth;
    edgeOriginY = -params.edgeTextureOriginY / edgeTextureHeight;
  }
  float edgeMaskUVData[4] = {edgeScaleX, edgeScaleY, edgeOriginX, edgeOriginY};
  fragmentUniformData->setData("GlassEdgeMaskUV", edgeMaskUVData);
}

}  // namespace tgfx
