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

#include "GLSLRectBlurFragmentProcessor.h"
#include "GeneratedOperators.h"
#include "core/utils/Log.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RectBlurFragmentProcessor::Make(BlockAllocator* allocator,
                                                                const Point& halfSizeOverSigma,
                                                                PMColor color,
                                                                const Matrix& coordMatrix) {
  DEBUG_ASSERT(halfSizeOverSigma.x > 0.0f && halfSizeOverSigma.y > 0.0f);
  return allocator->make<GLSLRectBlurFragmentProcessor>(halfSizeOverSigma, color, coordMatrix);
}

GLSLRectBlurFragmentProcessor::GLSLRectBlurFragmentProcessor(const Point& halfSizeOverSigma,
                                                             PMColor color,
                                                             const Matrix& coordMatrix)
    : RectBlurFragmentProcessor(halfSizeOverSigma, color, coordMatrix) {
}

void GLSLRectBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto coverageEntry = fragBuilder->appendFunction("shapeBlurRectCoverage");
  if (coverageEntry.empty()) {
    return;
  }

  auto halfSizeName = args.uniformHandler->addUniform("HalfSizeOverSigma", UniformFormat::Float2,
                                                      ShaderStage::Fragment);
  auto colorName = args.uniformHandler->addUniform("Color", UniformFormat::Float4,
                                                   ShaderStage::Fragment);

  auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("highp float coverage = %s(%s, %s);", coverageEntry.c_str(),
                           coordName.c_str(), halfSizeName.c_str());
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
  const auto coverage =
      ShapeBlurRectCoverageOp::Emit(ir, args.uniformHandler, nullptr, coord, halfSize);
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

void GLSLRectBlurFragmentProcessor::onSetData(UniformData*,
                                              UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("HalfSizeOverSigma", halfSizeOverSigma);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
