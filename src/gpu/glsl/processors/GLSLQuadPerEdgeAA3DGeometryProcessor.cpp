/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLSLQuadPerEdgeAA3DGeometryProcessor.h"

namespace tgfx {

PlacementPtr<QuadPerEdgeAA3DGeometryProcessor> QuadPerEdgeAA3DGeometryProcessor::Make(
    BlockBuffer* buffer, AAType aa, const Matrix3D& transfromMatrix, const Matrix& adjustMatrix) {
  return buffer->make<GLSLQuadPerEdgeAA3DGeometryProcessor>(aa, transfromMatrix, adjustMatrix);
}

GLSLQuadPerEdgeAA3DGeometryProcessor::GLSLQuadPerEdgeAA3DGeometryProcessor(
    AAType aa, const Matrix3D& transformMatrix, const Matrix& adjustMatrix)
    : QuadPerEdgeAA3DGeometryProcessor(aa, transformMatrix, adjustMatrix) {
}

void GLSLQuadPerEdgeAA3DGeometryProcessor::emitCode(EmitArgs& args) const {
  const auto vertBuilder = args.vertBuilder;
  const auto fragBuilder = args.fragBuilder;
  const auto varyingHandler = args.varyingHandler;
  const auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  if (aa == AAType::Coverage) {
    const auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), coverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  // According to the default fragment processor color rendering logic, the color needs to be
  // obtained from a uniform variable.
  const auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());

  const auto transformMatrixName =
      uniformHandler->addUniform("transformMatrix", UniformFormat::Float4x4, ShaderStage::Vertex);
  args.vertBuilder->codeAppendf("vec4 position_trans = %s * vec4(%s, 0.0, 1.0);",
                                transformMatrixName.c_str(), position.name().c_str());
  args.vertBuilder->codeAppend("vec2 position_ndc = position_trans.xy / position_trans.w;");
  // TODO: RichardJieChen
  // const auto adjustMatrixName = uniformHandler->addUniform("adjustMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  // args.vertBuilder->codeAppendf("position_ndc = (%s * vec3(position_ndc, 1.0)).xy;", adjustMatrixName.c_str());
  //
  args.vertBuilder->emitNormalizedPosition(position.name());
  //
  args.vertBuilder->codeAppend("gl_Position = vec4(position_ndc.x, position_ndc.y, 0.0, 1.0);");
}

void GLSLQuadPerEdgeAA3DGeometryProcessor::setData(UniformBuffer* vertexUniformBuffer,
                                                   UniformBuffer* fragmentUniformBuffer,
                                                   FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformBuffer, transformIter);
  fragmentUniformBuffer->setData("Color", defaultColor);
  vertexUniformBuffer->setData("transformMatrix", transfromMatrix);
  //TODO: RichardJieChen
  // vertexUniformBuffer->setData("adjustMatrix", adjustMatrix);
}

}  // namespace tgfx