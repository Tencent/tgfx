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

#include "GeneratedOperators.h"
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
  // The IR route is the only route: the whole shape computation (SDF, edge band, offset
  // distance, corner compensation, direction blend) lives in the two shared functions, and the
  // compile-time shape choice picks the one this draw uses.
  if (args.irBuilder == nullptr) {
    return;
  }
  if (args.inputValue == INVALID_VALUE_ID || args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  auto ir = args.irBuilder;
  if (shapeType == GlassShapeType::RoundedRect) {
    GlassSDFRoundedOp::UniformNames names = {};
    ir->StoreOutput(args.outputValue,
                    GlassSDFRoundedOp::Emit(ir, args.uniformHandler, &names, args.inputValue));
  } else {
    GlassSFDEllipseOp::UniformNames names = {};
    ir->StoreOutput(args.outputValue,
                    GlassSFDEllipseOp::Emit(ir, args.uniformHandler, &names, args.inputValue));
  }
}

void GLSLGlassSDFGeometryFragmentProcessor::onSetData(UniformData*,
                                                      UniformData* fragmentUniformData) const {
  float shapeData[4] = {params.halfW, params.halfH, params.cornerRadius, params.glassThickness};
  fragmentUniformData->setData("GlassShapeP0", shapeData);
  float effectData[4] = {params.refractionFactor, params.splay, params.glassThickness,
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
  auto gradientRadius = args.uniformHandler->addUniform("GlassGradientRadius", UniformFormat::Float,
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
  fragBuilder->codeAppendf("vec2 gradientStep = vec2(max(%s, 0.0001));", gradientRadius.c_str());
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
  fragBuilder->codeAppendf(
      "  float normalizedThickness = clamp(%s.z / max(min(halfW, halfH), 0.0001), 0.0, 1.0);",
      effect.c_str());
  // This UDF-only shallow-depth curve is intentional and scale-invariant. An external K scales both
  // glassThickness and minHalf, leaving normalizedThickness and therefore depthScale unchanged. At a
  // fixed shape size the curve is nonlinear in absolute depth, but it does not add a second K factor.
  // The SDF path does not use this profile, and this change does not claim profile equivalence.
  fragBuilder->codeAppend("  float depthScale = smoothstep(0.0, 0.1, normalizedThickness);");
  fragBuilder->codeAppendf("  float distance = %s.z * %s.x * depthScale * gradientWeight;",
                           effect.c_str(), effect.c_str());
  // Proximity can raise the profile above the physical bound; the refraction pass clamps the final
  // displacement to maxDisplacement, which also sizes the background-filter outset.
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
  float effectData[4] = {params.refractionFactor, params.splay, params.glassThickness,
                         params.edgeBandLayerPixels};
  fragmentUniformData->setData("GlassShapeP1", effectData);
  float gradientRadiusData = params.gradientSampleRadiusLayerPixels;
  fragmentUniformData->setData("GlassGradientRadius", gradientRadiusData);
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
