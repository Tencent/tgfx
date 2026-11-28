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

#include "GLSLRRectBlurFragmentProcessor.h"
#include "GeneratedOperators.h"
#include "GLSLShapeBlurFunctions.h"
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RRectBlurFragmentProcessor::Make(BlockAllocator* allocator,
                                                                 const Point& halfSizeOverSigma,
                                                                 const Point& cornerOverSigma,
                                                                 PMColor color,
                                                                 const Matrix& coordMatrix) {
  DEBUG_ASSERT(halfSizeOverSigma.x > 0.0f && halfSizeOverSigma.y > 0.0f &&
               cornerOverSigma.x <= halfSizeOverSigma.x &&
               cornerOverSigma.y <= halfSizeOverSigma.y);
  return allocator->make<GLSLRRectBlurFragmentProcessor>(
      halfSizeOverSigma, cornerOverSigma, color, coordMatrix,
      GetRRectBlurQuadratureCount(cornerOverSigma));
}

GLSLRRectBlurFragmentProcessor::GLSLRRectBlurFragmentProcessor(const Point& halfSizeOverSigma,
                                                               const Point& cornerOverSigma,
                                                               PMColor color,
                                                               const Matrix& coordMatrix,
                                                               int quadratureCount)
    : RRectBlurFragmentProcessor(halfSizeOverSigma, cornerOverSigma, color, coordMatrix,
                                 quadratureCount) {
}

void GLSLRRectBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  // The quadrature count is baked into the library function, so the count picks the function.
  const bool highQuadrature = quadratureCount >= 8;
  const char* coverageSymbol =
      highQuadrature ? "shapeBlurRRectCoverageHigh" : "shapeBlurRRectCoverageLow";
  auto coverageEntry = fragBuilder->appendFunction(coverageSymbol);
  if (coverageEntry.empty()) {
    return;
  }

  auto halfSizeName = args.uniformHandler->addUniform("HalfSizeOverSigma", UniformFormat::Float2,
                                                      ShaderStage::Fragment);
  auto cornerName = args.uniformHandler->addUniform("CornerOverSigma", UniformFormat::Float2,
                                                    ShaderStage::Fragment);
  auto colorName = args.uniformHandler->addUniform("Color", UniformFormat::Float4,
                                                   ShaderStage::Fragment);

  auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("highp float coverage = %s(%s, %s, %s);", coverageEntry.c_str(),
                           coordName.c_str(), halfSizeName.c_str(), cornerName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Only the input alpha is consumed; the shadow color comes from the uniform.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
  // TODO: Add dithering. The blurred coverage changes gradually from 1 to 0, which can quantize
  // into visible bands on an 8-bit target.

  if (args.irBuilder == nullptr || args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  auto ir = args.irBuilder;
  const auto coord =
      ir->InternValue(coordName, SLType::Float2, Precision::High, IRValueKind::Varying);
  const auto halfSize =
      ir->InternValue(halfSizeName, SLType::Float2, Precision::High, IRValueKind::Uniform);
  const auto corner =
      ir->InternValue(cornerName, SLType::Float2, Precision::High, IRValueKind::Uniform);
  const auto coverage =
      highQuadrature
          ? ShapeBlurRRectCoverageHighOp::Emit(ir, args.uniformHandler, nullptr, coord, halfSize,
                                               corner)
          : ShapeBlurRRectCoverageLowOp::Emit(ir, args.uniformHandler, nullptr, coord, halfSize,
                                              corner);
  const auto color =
      ir->InternValue(colorName, SLType::Float4, Precision::High, IRValueKind::Uniform);
  // A chain head has no input color; the text route treats that as white, so the IR names the
  // same constant.
  ValueID inputValue = args.inputValue;
  if (inputValue == INVALID_VALUE_ID) {
    const auto one = ir->CreateConstant(1.0f);
    inputValue = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
  }
  const auto coverageVec =
      ir->CreateConstruct(SLType::Float4, {coverage, coverage, coverage, coverage});
  const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, color, coverageVec);
  const auto inputAlpha = ir->CreateSwizzle(inputValue, SLType::Float4, "aaaa");
  const auto result = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float4, scaled, inputAlpha);
  ir->StoreOutput(args.outputValue, result);
}

void GLSLRRectBlurFragmentProcessor::onSetData(UniformData*,
                                               UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("HalfSizeOverSigma", halfSizeOverSigma);
  fragmentUniformData->setData("CornerOverSigma", cornerOverSigma);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
