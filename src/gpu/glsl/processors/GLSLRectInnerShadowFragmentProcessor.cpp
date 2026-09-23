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
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RectInnerShadowFragmentProcessor::Make(
    BlockAllocator* allocator, const Point& maskHalfOverSigma, const Point& shadowHalfSize,
    PMColor color, const Matrix& shadowCoordMatrix, const Matrix& maskCoordMatrix) {
  DEBUG_ASSERT(shadowHalfSize.x > 0.0f && shadowHalfSize.y > 0.0f && maskHalfOverSigma.x > 0.0f &&
               maskHalfOverSigma.y > 0.0f);
  if (shadowHalfSize.x <= 0.0f || shadowHalfSize.y <= 0.0f || maskHalfOverSigma.x <= 0.0f ||
      maskHalfOverSigma.y <= 0.0f) {
    return nullptr;
  }
  return allocator->make<GLSLRectInnerShadowFragmentProcessor>(
      maskHalfOverSigma, shadowHalfSize, color, shadowCoordMatrix, maskCoordMatrix);
}

GLSLRectInnerShadowFragmentProcessor::GLSLRectInnerShadowFragmentProcessor(
    const Point& maskHalfOverSigma, const Point& shadowHalfSize, PMColor color,
    const Matrix& shadowCoordMatrix, const Matrix& maskCoordMatrix)
    : RectInnerShadowFragmentProcessor(maskHalfOverSigma, shadowHalfSize, color, shadowCoordMatrix,
                                       maskCoordMatrix) {
}

void GLSLRectInnerShadowFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  AppendRectBlurFunctions(fragBuilder);
  AppendRectMaskFunctions(fragBuilder);

  auto maskHalfName = args.uniformHandler->addUniform("MaskHalfOverSigma", UniformFormat::Float2,
                                                      ShaderStage::Fragment);
  auto shadowHalfName = args.uniformHandler->addUniform("ShadowHalfSize", UniformFormat::Float2,
                                                        ShaderStage::Fragment);
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  auto shadowCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  auto maskCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[1]);
  fragBuilder->codeAppendf("float coverage = 1.0 - shapeBlurRectCoverage(%s, %s);",
                           maskCoordName.c_str(), maskHalfName.c_str());
  fragBuilder->codeAppendf("coverage *= shapeMaskCoverage(shapeMaskRectSDF(%s, %s));",
                           shadowCoordName.c_str(), shadowHalfName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Only the input alpha is consumed; the shadow color comes from the uniform.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
  // The blurred coverage changes gradually from 1 to 0, which can quantize into visible bands on an
  // 8-bit target. Dithering would hide them.
}

void GLSLRectInnerShadowFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                     UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("MaskHalfOverSigma", maskHalfOverSigma);
  fragmentUniformData->setData("ShadowHalfSize", shadowHalfSize);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
