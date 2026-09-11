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
#include "GLSLShapeBlurFunctions.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RRectBlurFragmentProcessor::Make(BlockAllocator* allocator,
                                                                 const Point& halfSizeOverSigma,
                                                                 const Point& cornerOverSigma,
                                                                 PMColor color,
                                                                 const Matrix* uvMatrix) {
  if (halfSizeOverSigma.x <= 0.0f || halfSizeOverSigma.y <= 0.0f) {
    return nullptr;
  }
  // The row half-width formula assumes each corner semi-axis fits inside the matching half size.
  // RRect::setRectRadii already enforces this, and MakeSpreadRRect re-normalizes through it, so a
  // violation means the caller bypassed that path.
  DEBUG_ASSERT(cornerOverSigma.x <= halfSizeOverSigma.x &&
               cornerOverSigma.y <= halfSizeOverSigma.y);
  return allocator->make<GLSLRRectBlurFragmentProcessor>(halfSizeOverSigma, cornerOverSigma, color,
                                                         uvMatrix);
}

GLSLRRectBlurFragmentProcessor::GLSLRRectBlurFragmentProcessor(const Point& halfSizeOverSigma,
                                                               const Point& cornerOverSigma,
                                                               PMColor color,
                                                               const Matrix* uvMatrix)
    : RRectBlurFragmentProcessor(halfSizeOverSigma, cornerOverSigma, color, uvMatrix) {
}

void GLSLRRectBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  AppendRoundRectBlurFunctions(fragBuilder);

  auto halfSizeName = args.uniformHandler->addUniform("HalfSizeOverSigma", UniformFormat::Float2,
                                                      ShaderStage::Fragment);
  auto cornerName = args.uniformHandler->addUniform("CornerOverSigma", UniformFormat::Float2,
                                                    ShaderStage::Fragment);
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("vec2 shapeCoord = %s;", coordName.c_str());
  // Points deeper than the kernel support (2.0) from every edge are fully covered, so the
  // quadrature can be skipped. The corner term keeps the test conservative: the arc pushes the
  // narrowest row inward by the corner semi-axis.
  fragBuilder->codeAppendf(
      "bool solidInterior = all(lessThan(abs(shapeCoord), %s - (%s + vec2(2.0))));",
      halfSizeName.c_str(), cornerName.c_str());
  fragBuilder->codeAppend("float coverage;");
  fragBuilder->codeAppend("if (solidInterior) {");
  fragBuilder->codeAppend("  coverage = 1.0;");
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppendf("  coverage = shapeBlurRoundRectCoverage(shapeCoord, %s, %s);",
                           halfSizeName.c_str(), cornerName.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Modulate by the input color so the paint's alpha reaches the shadow.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
}

void GLSLRRectBlurFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                               UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("HalfSizeOverSigma", halfSizeOverSigma);
  fragmentUniformData->setData("CornerOverSigma", cornerOverSigma);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
