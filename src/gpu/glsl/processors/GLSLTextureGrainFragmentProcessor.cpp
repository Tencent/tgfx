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
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLSLTextureGrainFragmentProcessor.h"
#include <string>
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/TextureGrainNoise.h"

namespace tgfx {

PlacementPtr<TextureGrainFragmentProcessor> TextureGrainFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> source,
    std::shared_ptr<TextureView> gradientView, const TextureGrainParams& params,
    const SamplingOptions& sampling, bool useHardwareDecal, const Matrix& coordMatrix) {
  if (allocator == nullptr || source == nullptr || gradientView == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLTextureGrainFragmentProcessor>(
      std::move(source), std::move(gradientView), params, sampling, useHardwareDecal, coordMatrix);
}

GLSLTextureGrainFragmentProcessor::GLSLTextureGrainFragmentProcessor(
    std::shared_ptr<TextureProxy> source, std::shared_ptr<TextureView> gradientView,
    const TextureGrainParams& params, const SamplingOptions& sampling, bool useHardwareDecal,
    const Matrix& coordMatrix)
    : TextureGrainFragmentProcessor(std::move(source), std::move(gradientView), params, sampling,
                                    useHardwareDecal, coordMatrix) {
}

static void AppendNoiseFunctions(FragmentShaderBuilder* fragBuilder, SamplerHandle gradientSampler,
                                 const std::string& gradientName, const std::string& noiseName,
                                 const std::string& fbmName) {
  auto tableSize = std::to_string(TextureGrainTableSize);
  auto tableSizeFloat = tableSize + ".0";
  auto lookup = fragBuilder->getTextureLookupExpression(
      gradientSampler, "(wrapped + vec2(0.5)) / " + tableSizeFloat);
  fragBuilder->addFunction(
      "highp vec4 " + gradientName +
      "(highp vec2 cell) {\n"
      "  highp vec2 wrapped = mod(cell, " +
      tableSizeFloat +
      ");\n"
      "  return " +
      lookup +
      " * 2.0 - 1.0;\n"
      "}\n\n"
      "highp vec2 " +
      noiseName +
      "(highp vec2 position, highp vec2 tableOffset) {\n"
      "  highp vec2 base = floor(position);\n"
      "  highp vec2 fraction = fract(position);\n"
      "  highp vec2 smoothValue = fraction * fraction * (3.0 - 2.0 * fraction);\n"
      "  highp vec4 g00 = " +
      gradientName +
      "(base + tableOffset);\n"
      "  highp vec4 g10 = " +
      gradientName +
      "(base + tableOffset + vec2(1.0, 0.0));\n"
      "  highp vec4 g01 = " +
      gradientName +
      "(base + tableOffset + vec2(0.0, 1.0));\n"
      "  highp vec4 g11 = " +
      gradientName +
      "(base + tableOffset + vec2(1.0, 1.0));\n"
      "  highp vec2 offsetX = fraction - vec2(1.0, 0.0);\n"
      "  highp vec2 offsetY = fraction - vec2(0.0, 1.0);\n"
      "  highp vec2 offsetXY = fraction - vec2(1.0, 1.0);\n"
      "  highp vec2 bottomLeft = vec2(dot(g00.rg, fraction), "
      "dot(g00.ba, fraction));\n"
      "  highp vec2 bottomRight = vec2(dot(g10.rg, offsetX), dot(g10.ba, "
      "offsetX));\n"
      "  highp vec2 topLeft = vec2(dot(g01.rg, offsetY), dot(g01.ba, "
      "offsetY));\n"
      "  highp vec2 topRight = vec2(dot(g11.rg, offsetXY), dot(g11.ba, "
      "offsetXY));\n"
      "  highp vec2 bottom = mix(bottomLeft, bottomRight, smoothValue.x);\n"
      "  highp vec2 top = mix(topLeft, topRight, smoothValue.x);\n"
      "  return mix(bottom, top, smoothValue.y);\n"
      "}\n\n"
      "highp vec2 " +
      fbmName +
      "(highp vec2 position, highp vec2 offset0, highp vec2 offset1, "
      "highp vec2 offset2) {\n"
      "  return " +
      noiseName + "(position, offset0) + 0.5 * " + noiseName +
      "(position * 2.0, offset1) + 0.25 * " + noiseName +
      "(position * 4.0, offset2);\n"
      "}\n");
}

static void AppendSourceSamplingFunction(FragmentShaderBuilder* fragBuilder,
                                         const std::vector<SamplerHandle>& sourceSamplers,
                                         const std::string& samplingInfoName,
                                         const std::string& colorMatrixName,
                                         const std::string& functionName, bool useHardwareDecal,
                                         const YUVTextureView* yuvTexture) {
  auto needsCoverage = !useHardwareDecal || yuvTexture != nullptr;
  std::string coverageCode;
  if (needsCoverage) {
    coverageCode = "  highp vec2 textureSize = " + samplingInfoName +
                   ".zw;\n"
                   "  highp float coverageX = min(clamp(sourcePosition.x + 0.5, 0.0, 1.0), "
                   "clamp(textureSize.x - sourcePosition.x + 0.5, 0.0, 1.0));\n"
                   "  highp float coverageY = min(clamp(sourcePosition.y + 0.5, 0.0, 1.0), "
                   "clamp(textureSize.y - sourcePosition.y + 0.5, 0.0, 1.0));\n"
                   "  highp float coverage = coverageX * coverageY;\n"
                   "  if (coverage <= 0.0) {\n"
                   "    return vec4(0.0);\n"
                   "  }\n";
  }
  std::string samplingCode;
  if (yuvTexture == nullptr) {
    auto lookup = fragBuilder->getTextureLookupExpression(sourceSamplers[0], "texturePosition");
    samplingCode = "  highp vec4 sampled = " + lookup + ";\n" +
                   (needsCoverage ? "  sampled *= coverage;\n" : "") + "  return sampled;\n";
  } else {
    auto yLookup = fragBuilder->getTextureLookupExpression(sourceSamplers[0], "texturePosition");
    auto uvLookup = fragBuilder->getTextureLookupExpression(sourceSamplers[1], "texturePosition");
    std::string yuvCode = "  highp float y = " + yLookup + ".r;\n";
    if (yuvTexture->yuvFormat() == YUVFormat::I420) {
      auto vLookup = fragBuilder->getTextureLookupExpression(sourceSamplers[2], "texturePosition");
      yuvCode += "  highp float u = " + uvLookup + ".r;\n";
      yuvCode += "  highp float v = " + vLookup + ".r;\n";
    } else {
      yuvCode += "  highp vec2 uv = " + uvLookup + ".ra;\n";
      yuvCode += "  highp float u = uv.x;\n";
      yuvCode += "  highp float v = uv.y;\n";
    }
    if (IsLimitedYUVColorRange(yuvTexture->yuvColorSpace())) {
      yuvCode += "  y -= (16.0 / 255.0);\n";
    }
    yuvCode += "  highp vec3 yuv = vec3(y, u - 0.5, v - 0.5);\n";
    yuvCode += "  highp vec3 rgb = clamp(" + colorMatrixName + " * yuv, 0.0, 1.0);\n";
    yuvCode +=
        needsCoverage ? "  return vec4(rgb * coverage, coverage);\n" : "  return vec4(rgb, 1.0);\n";
    samplingCode = std::move(yuvCode);
  }
  fragBuilder->addFunction("highp vec4 " + functionName + "(highp vec2 sourcePosition) {\n" +
                           coverageCode +
                           "  highp vec2 texturePosition = sourcePosition;\n"
                           "  if (" +
                           samplingInfoName +
                           ".x > 0.5) {\n"
                           "    texturePosition /= " +
                           samplingInfoName +
                           ".zw;\n"
                           "  }\n"
                           "  if (" +
                           samplingInfoName +
                           ".y > 0.5) {\n"
                           "    texturePosition.y = (" +
                           samplingInfoName + ".x > 0.5 ? 1.0 : " + samplingInfoName +
                           ".w) - texturePosition.y;\n"
                           "  }\n" +
                           samplingCode + "}\n");
}

void GLSLTextureGrainFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto common = args.uniformHandler->addUniform("TextureGrainCommon", UniformFormat::Float4,
                                                ShaderStage::Fragment);
  auto geometry = args.uniformHandler->addUniform("TextureGrainGeometry", UniformFormat::Float4,
                                                  ShaderStage::Fragment);
  auto sizes = args.uniformHandler->addUniform("TextureGrainSizes", UniformFormat::Float4,
                                               ShaderStage::Fragment);
  auto offsets01 = args.uniformHandler->addUniform("TextureGrainOffsets01", UniformFormat::Float4,
                                                   ShaderStage::Fragment);
  auto offsets2 = args.uniformHandler->addUniform("TextureGrainOffsets2", UniformFormat::Float4,
                                                  ShaderStage::Fragment);
  auto samplingInfo = args.uniformHandler->addUniform("TextureGrainSamplingInfo",
                                                      UniformFormat::Float4, ShaderStage::Fragment);

  auto sourceSamplerCount = this->sourceSamplerCount();
  const auto& textureSamplers = *args.textureSamplers;
  auto gradientSampler = textureSamplers[sourceSamplerCount];
  auto yuvTexture = getYUVTexture();
  std::string colorMatrixName;
  if (yuvTexture != nullptr) {
    colorMatrixName = args.uniformHandler->addUniform(
        "TextureGrainColorConversion", UniformFormat::Float3x3, ShaderStage::Fragment);
  }
  auto gradientName = fragBuilder->getMangledFunctionName("textureGrainGradient");
  auto noiseName = fragBuilder->getMangledFunctionName("textureGrainNoise");
  auto fbmName = fragBuilder->getMangledFunctionName("textureGrainFBM");
  AppendNoiseFunctions(fragBuilder, gradientSampler, gradientName, noiseName, fbmName);
  auto sampleSourceName = fragBuilder->getMangledFunctionName("textureGrainSampleSource");
  std::vector<SamplerHandle> sourceSamplers;
  sourceSamplers.reserve(sourceSamplerCount);
  for (size_t index = 0; index < sourceSamplerCount; ++index) {
    sourceSamplers.push_back(textureSamplers[index]);
  }
  AppendSourceSamplingFunction(fragBuilder, sourceSamplers, samplingInfo, colorMatrixName,
                               sampleSourceName, useHardwareDecal, yuvTexture);

  auto sourceCoord = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("highp vec2 sourcePoint = %s;", sourceCoord.c_str());
  fragBuilder->codeAppendf("highp vec2 localPoint = %s.zw + sourcePoint / %s.x;", geometry.c_str(),
                           sizes.c_str());
  fragBuilder->codeAppendf("highp vec2 grainPoint = (localPoint - %s.xy) * %s.xy;",
                           geometry.c_str(), common.c_str());
  fragBuilder->codeAppendf("highp vec2 grainNoise = %s(grainPoint, %s.xy, %s.zw, %s.xy);",
                           fbmName.c_str(), offsets01.c_str(), offsets01.c_str(), offsets2.c_str());
  fragBuilder->codeAppendf("highp vec2 grainOffset = %s.z * grainNoise;", common.c_str());
  if (!params.clipToShape) {
    fragBuilder->codeAppendf("vec4 displacedColor = %s(sourcePoint + grainOffset);",
                             sampleSourceName.c_str());
    if (sourceProxy->isAlphaOnly()) {
      fragBuilder->codeAppendf("%s = displacedColor.a * %s;", args.outputColor.c_str(),
                               args.inputColor.c_str());
    } else {
      fragBuilder->codeAppendf("%s = displacedColor * %s.a;", args.outputColor.c_str(),
                               args.inputColor.c_str());
    }
    return;
  }

  fragBuilder->codeAppendf("vec4 originalColor = %s(sourcePoint);", sampleSourceName.c_str());
  fragBuilder->codeAppendf("vec4 displacedColor = %s(sourcePoint + grainOffset);",
                           sampleSourceName.c_str());
  fragBuilder->codeAppend("vec4 clippedColor = originalColor;");
  fragBuilder->codeAppend("if (originalColor.a > 0.0 && displacedColor.a >= (1.0 / 255.0)) {");
  fragBuilder->codeAppend(
      "  highp vec3 displacedRgb = min(displacedColor.rgb, "
      "vec3(displacedColor.a));");
  fragBuilder->codeAppend(
      "  clippedColor.rgb = displacedRgb * (originalColor.a / displacedColor.a);");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("clippedColor.a = originalColor.a;");
  fragBuilder->codeAppend("clippedColor.rgb = min(clippedColor.rgb, vec3(clippedColor.a));");
  if (sourceProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = clippedColor.a * %s;", args.outputColor.c_str(),
                             args.inputColor.c_str());
  } else {
    fragBuilder->codeAppendf("%s = clippedColor * %s.a;", args.outputColor.c_str(),
                             args.inputColor.c_str());
  }
}

void GLSLTextureGrainFragmentProcessor::onSetData(UniformData*,
                                                  UniformData* fragmentUniformData) const {
  auto radiusSource = params.radius * params.pixelsPerLocalUnit;
  float commonData[4] = {params.frequencyX, params.frequencyY, radiusSource, 0.0f};
  fragmentUniformData->setData("TextureGrainCommon", commonData);

  float geometryData[4] = {params.nodeCenter.x, params.nodeCenter.y, params.sourceOrigin.x,
                           params.sourceOrigin.y};
  fragmentUniformData->setData("TextureGrainGeometry", geometryData);

  float sizeData[4] = {params.pixelsPerLocalUnit, static_cast<float>(sourceProxy->width()),
                       static_cast<float>(sourceProxy->height()), 0.0f};
  fragmentUniformData->setData("TextureGrainSizes", sizeData);

  auto sourceView = sourceProxy->getTextureView();
  auto normalized =
      sourceView == nullptr || sourceView->getTexture()->type() != TextureType::Rectangle;
  auto origin = sourceView != nullptr ? sourceView->origin() : sourceProxy->origin();
  float samplingData[4] = {
      normalized ? 1.0f : 0.0f, origin == ImageOrigin::BottomLeft ? 1.0f : 0.0f,
      static_cast<float>(sourceView != nullptr ? sourceView->width() : sourceProxy->width()),
      static_cast<float>(sourceView != nullptr ? sourceView->height() : sourceProxy->height())};
  fragmentUniformData->setData("TextureGrainSamplingInfo", samplingData);

  auto offset0 = TextureGrainSeedOffset(params.seed, 0);
  auto offset1 = TextureGrainSeedOffset(params.seed, 1);
  auto offset2 = TextureGrainSeedOffset(params.seed, 2);
  float offsetData01[4] = {offset0.x, offset0.y, offset1.x, offset1.y};
  fragmentUniformData->setData("TextureGrainOffsets01", offsetData01);
  float offsetData2[4] = {offset2.x, offset2.y, 0.0f, 0.0f};
  fragmentUniformData->setData("TextureGrainOffsets2", offsetData2);

  auto yuvTexture = getYUVTexture();
  if (yuvTexture != nullptr) {
    fragmentUniformData->setData("TextureGrainColorConversion",
                                 GetYUVColorConversionMatrix(yuvTexture->yuvColorSpace()));
  }
}

}  // namespace tgfx
