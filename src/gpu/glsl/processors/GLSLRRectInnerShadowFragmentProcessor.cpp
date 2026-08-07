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

#include "GLSLRRectInnerShadowFragmentProcessor.h"
#include "GLSLShapeBlurFunctions.h"
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RRectInnerShadowFragmentProcessor::Make(
    BlockAllocator* allocator, const Point& shadowHalfOverSigma, const Point& shadowCornerOverSigma,
    const Point& shadowCenterOffset, const Point& invSigma, const Point& maskHalfSize,
    float maskCornerRadius, PMColor color, const Matrix* uvMatrix) {
  if (maskHalfSize.x <= 0.0f || maskHalfSize.y <= 0.0f) {
    return nullptr;
  }
  if (shadowHalfOverSigma.x <= 0.0f || shadowHalfOverSigma.y <= 0.0f) {
    return nullptr;
  }
  // The row half-width formula assumes each corner semi-axis fits inside the matching half size.
  // RRect::setRectRadii already enforces this, and MakeSpreadRRect re-normalizes through it, so a
  // violation means the caller bypassed that path.
  DEBUG_ASSERT(shadowCornerOverSigma.x <= shadowHalfOverSigma.x &&
               shadowCornerOverSigma.y <= shadowHalfOverSigma.y);
  return allocator->make<GLSLRRectInnerShadowFragmentProcessor>(
      shadowHalfOverSigma, shadowCornerOverSigma, shadowCenterOffset, invSigma, maskHalfSize,
      maskCornerRadius, color, uvMatrix);
}

GLSLRRectInnerShadowFragmentProcessor::GLSLRRectInnerShadowFragmentProcessor(
    const Point& shadowHalfOverSigma, const Point& shadowCornerOverSigma,
    const Point& shadowCenterOffset, const Point& invSigma, const Point& maskHalfSize,
    float maskCornerRadius, PMColor color, const Matrix* uvMatrix)
    : RRectInnerShadowFragmentProcessor(shadowHalfOverSigma, shadowCornerOverSigma,
                                        shadowCenterOffset, invSigma, maskHalfSize,
                                        maskCornerRadius, color, uvMatrix) {
}

void GLSLRRectInnerShadowFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  AppendRoundRectBlurFunctions(fragBuilder);
  AppendRoundRectMaskFunctions(fragBuilder);

  auto shadowHalfName = args.uniformHandler->addUniform(
      "ShadowHalfOverSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto shadowCornerName = args.uniformHandler->addUniform(
      "ShadowCornerOverSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto shadowOffsetName = args.uniformHandler->addUniform(
      "ShadowCenterOffset", UniformFormat::Float2, ShaderStage::Fragment);
  auto invSigmaName =
      args.uniformHandler->addUniform("InvSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto maskHalfName =
      args.uniformHandler->addUniform("MaskHalfSize", UniformFormat::Float2, ShaderStage::Fragment);
  auto maskCornerName = args.uniformHandler->addUniform("MaskCornerRadius", UniformFormat::Float,
                                                        ShaderStage::Fragment);
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("vec2 maskCoord = %s;", coordName.c_str());
  fragBuilder->codeAppendf("vec2 shadowCoord = (maskCoord - %s) * %s;", shadowOffsetName.c_str(),
                           invSigmaName.c_str());
  // No interior skip here: the complement is zero deep inside the shape, which is exactly where the
  // mask clips the result away, so the branch would save nothing.
  fragBuilder->codeAppendf(
      "float coverage = 1.0 - shapeBlurRoundRectCoverage(shadowCoord, %s, %s);",
      shadowHalfName.c_str(), shadowCornerName.c_str());
  fragBuilder->codeAppendf(
      "coverage *= shapeMaskCoverage(shapeMaskRoundRectSDF(maskCoord, %s, %s));",
      maskHalfName.c_str(), maskCornerName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Modulate by the input color so the paint's alpha reaches the shadow.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
}

void GLSLRRectInnerShadowFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                      UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("ShadowHalfOverSigma", shadowHalfOverSigma);
  fragmentUniformData->setData("ShadowCornerOverSigma", shadowCornerOverSigma);
  fragmentUniformData->setData("ShadowCenterOffset", shadowCenterOffset);
  fragmentUniformData->setData("InvSigma", invSigma);
  fragmentUniformData->setData("MaskHalfSize", maskHalfSize);
  fragmentUniformData->setData("MaskCornerRadius", maskCornerRadius);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
