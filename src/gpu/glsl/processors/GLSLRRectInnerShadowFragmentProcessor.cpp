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
    const Point& maskHalfSize, const Point& maskCornerRadius, PMColor color,
    const Matrix& maskCoordMatrix, const Matrix& shadowCoordMatrix, int quadratureCount) {
  DEBUG_ASSERT(maskHalfSize.x > 0.0f && maskHalfSize.y > 0.0f && shadowHalfOverSigma.x > 0.0f &&
               shadowHalfOverSigma.y > 0.0f && quadratureCount > 0 &&
               maskCornerRadius.x <= maskHalfSize.x && maskCornerRadius.y <= maskHalfSize.y &&
               shadowCornerOverSigma.x <= shadowHalfOverSigma.x &&
               shadowCornerOverSigma.y <= shadowHalfOverSigma.y);
  if (maskHalfSize.x <= 0.0f || maskHalfSize.y <= 0.0f || shadowHalfOverSigma.x <= 0.0f ||
      shadowHalfOverSigma.y <= 0.0f || quadratureCount <= 0 ||
      maskCornerRadius.x > maskHalfSize.x || maskCornerRadius.y > maskHalfSize.y ||
      shadowCornerOverSigma.x > shadowHalfOverSigma.x ||
      shadowCornerOverSigma.y > shadowHalfOverSigma.y) {
    return nullptr;
  }
  return allocator->make<GLSLRRectInnerShadowFragmentProcessor>(
      shadowHalfOverSigma, shadowCornerOverSigma, maskHalfSize, maskCornerRadius, color,
      maskCoordMatrix, shadowCoordMatrix, quadratureCount);
}

GLSLRRectInnerShadowFragmentProcessor::GLSLRRectInnerShadowFragmentProcessor(
    const Point& shadowHalfOverSigma, const Point& shadowCornerOverSigma, const Point& maskHalfSize,
    const Point& maskCornerRadius, PMColor color, const Matrix& maskCoordMatrix,
    const Matrix& shadowCoordMatrix, int quadratureCount)
    : RRectInnerShadowFragmentProcessor(shadowHalfOverSigma, shadowCornerOverSigma, maskHalfSize,
                                        maskCornerRadius, color, maskCoordMatrix, shadowCoordMatrix,
                                        quadratureCount) {
}

void GLSLRRectInnerShadowFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  AppendRRectBlurFunctions(fragBuilder, quadratureCount);
  AppendRRectMaskFunctions(fragBuilder);

  auto shadowHalfName = args.uniformHandler->addUniform(
      "ShadowHalfOverSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto shadowCornerName = args.uniformHandler->addUniform(
      "ShadowCornerOverSigma", UniformFormat::Float2, ShaderStage::Fragment);
  auto maskHalfName =
      args.uniformHandler->addUniform("MaskHalfSize", UniformFormat::Float2, ShaderStage::Fragment);
  auto maskCornerName = args.uniformHandler->addUniform("MaskCornerRadius", UniformFormat::Float2,
                                                        ShaderStage::Fragment);
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  auto maskCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  auto shadowCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[1]);
  fragBuilder->codeAppendf("float coverage = 1.0 - shapeBlurRRectCoverage(%s, %s, %s);",
                           shadowCoordName.c_str(), shadowHalfName.c_str(),
                           shadowCornerName.c_str());
  fragBuilder->codeAppendf("coverage *= shapeMaskCoverage(shapeMaskRRectSDF(%s, %s, %s));",
                           maskCoordName.c_str(), maskHalfName.c_str(), maskCornerName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Only the input alpha is consumed; the shadow color comes from the uniform.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
  // The blurred coverage changes gradually from 1 to 0, which can quantize into visible bands on an
  // 8-bit target. Dithering would hide them.
}

void GLSLRRectInnerShadowFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                      UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("ShadowHalfOverSigma", shadowHalfOverSigma);
  fragmentUniformData->setData("ShadowCornerOverSigma", shadowCornerOverSigma);
  fragmentUniformData->setData("MaskHalfSize", maskHalfSize);
  fragmentUniformData->setData("MaskCornerRadius", maskCornerRadius);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
