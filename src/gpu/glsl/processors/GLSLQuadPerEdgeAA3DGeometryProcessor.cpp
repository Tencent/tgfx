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

static constexpr char UniformTransformMatrixName[] = "transformMatrix";
static constexpr char UniformNdcScaleName[] = "ndcScale";
static constexpr char UniformNdcOffsetName[] = "ndcOffset";

PlacementPtr<Transform3DGeometryProcessor> Transform3DGeometryProcessor::Make(
    BlockBuffer* buffer, AAType aa, const Matrix3D& matrix, const Vec2& ndcScale,
    const Vec2& ndcOffset) {
  return buffer->make<GLSLQuadPerEdgeAA3DGeometryProcessor>(aa, matrix, ndcScale, ndcOffset);
}

GLSLQuadPerEdgeAA3DGeometryProcessor::GLSLQuadPerEdgeAA3DGeometryProcessor(AAType aa,
                                                                           const Matrix3D& matrix,
                                                                           const Vec2& ndcScale,
                                                                           const Vec2& ndcOffset)
    : Transform3DGeometryProcessor(aa, matrix, ndcScale, ndcOffset) {
}

void GLSLQuadPerEdgeAA3DGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), coverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  // The default fragment processor color rendering logic requires a color uniform.
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  auto transformMatrixName = uniformHandler->addUniform(
      UniformTransformMatrixName, UniformFormat::Float4x4, ShaderStage::Vertex);
  args.vertBuilder->codeAppendf("vec4 clipPoint = %s * vec4(%s, 0.0, 1.0);",
                                transformMatrixName.c_str(), position.name().c_str());
  auto ndcScaleName =
      uniformHandler->addUniform(UniformNdcScaleName, UniformFormat::Float2, ShaderStage::Vertex);
  args.vertBuilder->codeAppendf("vec4 clipScale = vec4(%s.xy, 1.0, 1.0);", ndcScaleName.c_str());
  auto ndcOffsetName =
      uniformHandler->addUniform(UniformNdcOffsetName, UniformFormat::Float2, ShaderStage::Vertex);
  args.vertBuilder->codeAppendf("vec4 clipOffset = vec4((%s * clipPoint.w).xy, 0.0, 0.0);",
                                ndcOffsetName.c_str());
  args.vertBuilder->codeAppend("gl_Position = clipPoint * clipScale + clipOffset;");
}

void GLSLQuadPerEdgeAA3DGeometryProcessor::setData(UniformData* vertexUniformData,
                                                   UniformData* fragmentUniformData,
                                                   FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  fragmentUniformData->setData("Color", defaultColor);
  vertexUniformData->setData("transformMatrix", matrix);
  vertexUniformData->setData("ndcScale", ndcScale);
  vertexUniformData->setData("ndcOffset", ndcOffset);
}

}  // namespace tgfx