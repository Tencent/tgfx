/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLDefaultGeometryProcessor.h"

namespace tgfx {
PlacementPtr<DefaultGeometryProcessor> DefaultGeometryProcessor::Make(BlockAllocator* allocator,
                                                                      PMColor color, int width,
                                                                      int height, AAType aa,
                                                                      const Matrix& viewMatrix,
                                                                      const Matrix& uvMatrix) {
  return allocator->make<GLSLDefaultGeometryProcessor>(color, width, height, aa, viewMatrix,
                                                       uvMatrix);
}

GLSLDefaultGeometryProcessor::GLSLDefaultGeometryProcessor(PMColor color, int width, int height,
                                                           AAType aa, const Matrix& viewMatrix,
                                                           const Matrix& uvMatrix)
    : DefaultGeometryProcessor(color, width, height, aa, viewMatrix, uvMatrix) {
}

void GLSLDefaultGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto matrixName =
      args.uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  if (args.gpUniforms) {
    args.gpUniforms->add("Matrix", matrixName);
  }

  std::string coverageFsIn;
  std::string coverageVsOut;
  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    coverageFsIn = coverageVar.fsIn();
    coverageVsOut = coverageVar.vsOut();
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageFsIn);
    }
  }

  std::string positionName = "position";
  static const std::string kDefaultGPVert = R"GLSL(
void TGFX_DefaultGP_VS(vec2 inPosition, mat3 matrix,
#ifdef TGFX_GP_DEFAULT_COVERAGE_AA
                        float inCoverage, out float vCoverage,
#endif
                        out vec2 position) {
    position = (matrix * vec3(inPosition, 1.0)).xy;
#ifdef TGFX_GP_DEFAULT_COVERAGE_AA
    vCoverage = inCoverage;
#endif
}
)GLSL";
  vertBuilder->addFunction(kDefaultGPVert);
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_DefaultGP_VS(" + std::string(position.name()) + ", " + matrixName;
  if (aa == AAType::Coverage) {
    call += ", " + std::string(coverage.name()) + ", " + coverageVsOut;
  }
  call += ", " + positionName + ");";
  vertBuilder->codeAppend(call);

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  if (args.gpUniforms) {
    args.gpUniforms->add("Color", colorName);
  }

  args.vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLDefaultGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix, vertexUniformData, transformIter);
  fragmentUniformData->setData("Color", color);
  vertexUniformData->setData("Matrix", viewMatrix);
}
}  // namespace tgfx
