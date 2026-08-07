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

#include "GLSLRectInnerShadowFragmentProcessor.h"
#include "GLSLShapeBlurFunctions.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RectInnerShadowFragmentProcessor::Make(
    BlockAllocator* allocator, const Point& shadowHalfOverSigma, const Point& shadowCenterOffset,
    const Point& invSigma, const Point& maskHalfSize, PMColor color, const Matrix* uvMatrix) {
  if (maskHalfSize.x <= 0.0f || maskHalfSize.y <= 0.0f) {
    return nullptr;
  }
  if (shadowHalfOverSigma.x <= 0.0f || shadowHalfOverSigma.y <= 0.0f) {
    return nullptr;
  }
  return allocator->make<GLSLRectInnerShadowFragmentProcessor>(
      shadowHalfOverSigma, shadowCenterOffset, invSigma, maskHalfSize, color, uvMatrix);
}

GLSLRectInnerShadowFragmentProcessor::GLSLRectInnerShadowFragmentProcessor(
    const Point& shadowHalfOverSigma, const Point& shadowCenterOffset, const Point& invSigma,
    const Point& maskHalfSize, PMColor color, const Matrix* uvMatrix)
    : RectInnerShadowFragmentProcessor(shadowHalfOverSigma, shadowCenterOffset, invSigma,
                                       maskHalfSize, color, uvMatrix) {
}

void GLSLRectInnerShadowFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  AppendShapeBlurFunctions(fragBuilder);
  AppendShapeMaskFunctions(fragBuilder);

  auto shadowHalfName = args.uniformHandler->addUniform(
      "ShadowHalfOverSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto shadowOffsetName = args.uniformHandler->addUniform(
      "ShadowCenterOffset", UniformFormat::Float2, ShaderStage::Fragment);
  auto invSigmaName =
      args.uniformHandler->addUniform("InvSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto maskHalfName =
      args.uniformHandler->addUniform("MaskHalfSize", UniformFormat::Float2, ShaderStage::Fragment);
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("vec2 maskCoord = %s;", coordName.c_str());
  fragBuilder->codeAppendf("vec2 shadowCoord = (maskCoord - %s) * %s;", shadowOffsetName.c_str(),
                           invSigmaName.c_str());
  fragBuilder->codeAppendf("float coverage = 1.0 - shapeBlurRectCoverage(shadowCoord, %s);",
                           shadowHalfName.c_str());
  fragBuilder->codeAppendf("coverage *= shapeMaskCoverage(shapeMaskRectSDF(maskCoord, %s));",
                           maskHalfName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Modulate by the input color so the paint's alpha reaches the shadow.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
}

void GLSLRectInnerShadowFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                     UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("ShadowHalfOverSigma", shadowHalfOverSigma);
  fragmentUniformData->setData("ShadowCenterOffset", shadowCenterOffset);
  fragmentUniformData->setData("InvSigma", invSigma);
  fragmentUniformData->setData("MaskHalfSize", maskHalfSize);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
