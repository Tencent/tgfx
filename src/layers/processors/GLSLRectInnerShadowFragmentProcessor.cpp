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
#include "GeneratedOperators.h"
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RectInnerShadowFragmentProcessor::Make(
    BlockAllocator* allocator, const Point& shadowHalfSize, const Point& maskHalfOverSigma,
    PMColor color, const Matrix& shadowCoordMatrix, const Matrix& maskCoordMatrix) {
  DEBUG_ASSERT(shadowHalfSize.x > 0.0f && shadowHalfSize.y > 0.0f && maskHalfOverSigma.x > 0.0f &&
               maskHalfOverSigma.y > 0.0f);
  return allocator->make<GLSLRectInnerShadowFragmentProcessor>(
      shadowHalfSize, maskHalfOverSigma, color, shadowCoordMatrix, maskCoordMatrix);
}

GLSLRectInnerShadowFragmentProcessor::GLSLRectInnerShadowFragmentProcessor(
    const Point& shadowHalfSize, const Point& maskHalfOverSigma, PMColor color,
    const Matrix& shadowCoordMatrix, const Matrix& maskCoordMatrix)
    : RectInnerShadowFragmentProcessor(shadowHalfSize, maskHalfOverSigma, color, shadowCoordMatrix,
                                       maskCoordMatrix) {
}

void GLSLRectInnerShadowFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto blurEntry = fragBuilder->appendFunction("shapeBlurRectCoverage");
  auto maskSdfEntry = fragBuilder->appendFunction("shapeMaskRectSDF");
  auto maskCoverageEntry = fragBuilder->appendFunction("shapeMaskCoverage");
  if (blurEntry.empty() || maskSdfEntry.empty() || maskCoverageEntry.empty()) {
    return;
  }

  auto maskHalfName = args.uniformHandler->addUniform("MaskHalfOverSigma", UniformFormat::Float2,
                                                      ShaderStage::Fragment);
  auto shadowHalfName = args.uniformHandler->addUniform("ShadowHalfSize", UniformFormat::Float2,
                                                        ShaderStage::Fragment);
  auto colorName = args.uniformHandler->addUniform("Color", UniformFormat::Float4,
                                                   ShaderStage::Fragment);

  auto shadowCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  auto maskCoordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[1]);
  fragBuilder->codeAppendf("highp float coverage = 1.0 - %s(%s, %s);", blurEntry.c_str(),
                           maskCoordName.c_str(), maskHalfName.c_str());
  fragBuilder->codeAppendf("coverage *= %s(%s(%s, %s));", maskCoverageEntry.c_str(),
                           maskSdfEntry.c_str(), shadowCoordName.c_str(), shadowHalfName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Only the input alpha is consumed; the shadow color comes from the uniform.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
  // TODO: Add dithering. The blurred coverage changes gradually from 1 to 0, which can quantize
  // into visible bands on an 8-bit target.

  if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  auto ir = args.irBuilder;
  const auto maskCoord =
      ir->InternValue(maskCoordName, SLType::Float2, Precision::High, IRValueKind::Varying);
  const auto maskHalf =
      ir->InternValue(maskHalfName, SLType::Float2, Precision::High, IRValueKind::Uniform);
  const auto shadowCoord =
      ir->InternValue(shadowCoordName, SLType::Float2, Precision::High, IRValueKind::Varying);
  const auto shadowHalf =
      ir->InternValue(shadowHalfName, SLType::Float2, Precision::High, IRValueKind::Uniform);
  const auto blur = ShapeBlurRectCoverageOp::Emit(ir, args.uniformHandler, nullptr, maskCoord,
                                                  maskHalf);
  const auto one = ir->CreateConstant(1.0f);
  const auto oneVec = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
  const auto blurVec = ir->CreateConstruct(SLType::Float4, {blur, blur, blur, blur});
  const auto inverted = ir->CreateBinary(IRBinaryOp::Sub, SLType::Float4, oneVec, blurVec);
  const auto sdf =
      ShapeMaskRectSDFOp::Emit(ir, args.uniformHandler, nullptr, shadowCoord, shadowHalf);
  const auto maskCoverage = ShapeMaskCoverageOp::Emit(ir, args.uniformHandler, nullptr, sdf);
  const auto maskCoverageVec =
      ir->CreateConstruct(SLType::Float4, {maskCoverage, maskCoverage, maskCoverage, maskCoverage});
  const auto coverage = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, inverted, maskCoverageVec);
  const auto color =
      ir->InternValue(colorName, SLType::Float4, Precision::High, IRValueKind::Uniform);
  // A chain head has no input color; the text route treats that as white, so the IR names the
  // same constant.
  ValueID inputValue = args.inputValue;
  if (inputValue == INVALID_VALUE_ID) {
    const auto white = ir->CreateConstant(1.0f);
    inputValue = ir->CreateConstruct(SLType::Float4, {white, white, white, white});
  }
  const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, color, coverage);
  const auto inputAlpha = ir->CreateSwizzle(inputValue, SLType::Float4, "aaaa");
  const auto result = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, scaled, inputAlpha);
  ir->StoreOutput(args.outputValue, result);
}

void GLSLRectInnerShadowFragmentProcessor::onSetData(UniformData*,
                                                     UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("MaskHalfOverSigma", maskHalfOverSigma);
  fragmentUniformData->setData("ShadowHalfSize", shadowHalfSize);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
