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

#include "GLSLHairlineLineGeometryProcessor.h"
#include "gpu/AAType.h"

namespace tgfx {

PlacementPtr<HairlineLineGeometryProcessor> HairlineLineGeometryProcessor::Make(
    BlockAllocator* allocator, const PMColor& color, const Matrix& viewMatrix,
    std::optional<Matrix> uvMatrix, float coverage, AAType aaType) {
  return allocator->make<GLSLHairlineLineGeometryProcessor>(color, viewMatrix, uvMatrix, coverage,
                                                            aaType);
}

GLSLHairlineLineGeometryProcessor::GLSLHairlineLineGeometryProcessor(const PMColor& color,
                                                                     const Matrix& viewMatrix,
                                                                     std::optional<Matrix> uvMatrix,
                                                                     float coverage, AAType aaType)
    : HairlineLineGeometryProcessor(color, viewMatrix, uvMatrix, coverage, aaType) {
}

void GLSLHairlineLineGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit vertex attributes
  varyingHandler->emitAttributes(*this);

  // Transform vertex position by view matrix
  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  // Pass edge distance to fragment shader for anti-aliasing
  auto edgeVarying = varyingHandler->addVarying("EdgeDistance", SLType::Float);
  if (args.gpVaryings) {
    args.gpVaryings->add("EdgeDistance", edgeVarying.fsIn());
  }

  // Half-migrated: VS function body lives in hairline_line_geometry.vert.glsl (injected by
  // ModularProgramBuilder via includeVSModule based on shaderFunctionFile()). emitTransforms()
  // needs to operate on the transformed position, so we emit the function call here rather
  // than in buildVSCallExpr().
  std::string positionName = "transformedPosition";
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_HairlineLineGP_VS(" + std::string(position.name()) + ", " +
                     std::string(edgeDistance.name()) + ", " + matrixName + ", " +
                     edgeVarying.vsOut() + ", " + positionName + ");";
  vertBuilder->codeAppend(call);

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(positionName, SLType::Float2));

  // Output color and coverage
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  if (args.gpUniforms) {
    args.gpUniforms->add("Color", colorName);
  }
  auto coverageScale =
      uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
  if (args.gpUniforms) {
    args.gpUniforms->add("Coverage", coverageScale);
  }
}

void GLSLHairlineLineGeometryProcessor::setData(UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData,
                                                FPCoordTransformIter* transformIter) const {
  if (uvMatrix.has_value()) {
    setTransformDataHelper(*uvMatrix, vertexUniformData, transformIter);
  }
  fragmentUniformData->setData("Color", color);
  vertexUniformData->setData("Matrix", viewMatrix);
  fragmentUniformData->setData("Coverage", coverage);
}

}  // namespace tgfx
