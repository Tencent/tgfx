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
#include "GLSLShapeBlurFunctions.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> RectBlurFragmentProcessor::Make(BlockAllocator* allocator,
                                                                const Point& halfSizeOverSigma,
                                                                PMColor color,
                                                                const Matrix* uvMatrix) {
  if (halfSizeOverSigma.x <= 0.0f || halfSizeOverSigma.y <= 0.0f) {
    return nullptr;
  }
  return allocator->make<GLSLRectBlurFragmentProcessor>(halfSizeOverSigma, color, uvMatrix);
}

GLSLRectBlurFragmentProcessor::GLSLRectBlurFragmentProcessor(const Point& halfSizeOverSigma,
                                                             PMColor color, const Matrix* uvMatrix)
    : RectBlurFragmentProcessor(halfSizeOverSigma, color, uvMatrix) {
}

void GLSLRectBlurFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  AppendShapeBlurFunctions(fragBuilder);

  auto halfSizeName = args.uniformHandler->addUniform("HalfSizeOverSigma", UniformFormat::Float2,
                                                      ShaderStage::Fragment);
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("float coverage = shapeBlurRectCoverage(%s, %s);", coordName.c_str(),
                           halfSizeName.c_str());
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(), colorName.c_str());
  // Modulate by the input color so the paint's alpha reaches the shadow.
  fragBuilder->codeAppendf("%s *= %s.a;", args.outputColor.c_str(), args.inputColor.c_str());
}

void GLSLRectBlurFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                              UniformData* fragmentUniformData) const {
  fragmentUniformData->setData("HalfSizeOverSigma", halfSizeOverSigma);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
