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

#include "GLSLBezierCoverGeometryProcessor.h"

namespace tgfx {
PlacementPtr<BezierCoverGeometryProcessor> BezierCoverGeometryProcessor::Make(
    BlockAllocator* allocator, PMColor color, const Matrix& viewMatrix, const Matrix& uvMatrix) {
  return allocator->make<GLSLBezierCoverGeometryProcessor>(color, viewMatrix, uvMatrix);
}

GLSLBezierCoverGeometryProcessor::GLSLBezierCoverGeometryProcessor(PMColor color,
                                                                   const Matrix& viewMatrix,
                                                                   const Matrix& uvMatrix)
    : BezierCoverGeometryProcessor(color, viewMatrix, uvMatrix) {
}

void GLSLBezierCoverGeometryProcessor::emitCode(EmitArgs& args) const {
  // Cover pass: forward the device-bounds quad position through the view matrix, emit the uv
  // coordinates for any upstream FP (shader / colour filter / mask filter) supplied by the
  // draw op, and write the brush colour as the fragment output. Upstream FPs in the chain run
  // before this processor and composite against outputColor via the standard ProgramBuilder
  // wiring — when there are no FPs, the brush colour is the final colour.
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->codeAppendf("highp vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  // Default coverage of 1.0; the FP chain (mask filter, clip) may multiply this further.
  fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  // Brush colour uniform — matches GLSLDefaultGeometryProcessor. Without this uniform the
  // cover pass would emit a hard-coded colour (white) and silently ignore the brush.
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());

  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLBezierCoverGeometryProcessor::setData(UniformData* vertexUniformData,
                                               UniformData* fragmentUniformData,
                                               FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix, vertexUniformData, transformIter);
  vertexUniformData->setData("Matrix", viewMatrix);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
