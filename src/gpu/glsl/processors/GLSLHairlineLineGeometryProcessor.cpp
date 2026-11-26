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

#include "gpu/ir/ProgramIREmit.h"
#include "GLSLHairlineLineGeometryProcessor.h"
#include "GeneratedOperators.h"
#include "gpu/AAType.h"
#include "gpu/VertexShaderBuilder.h"

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

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  auto colorName = uniformHandler->addUniform("Color", UniformFormat::Float4,
                                              ShaderStage::Fragment);
  // The position's transform lives in the IR block below; the text path is gone.
  auto edgeVarying = varyingHandler->addVarying("EdgeDistance", SLType::Float);
  HairlineLineCoverageOp::UniformNames coverageNames = {};
  if (args.irBuilder != nullptr) {
    HairlineLineCoverageOp::EnsureUniforms(args.irBuilder, uniformHandler, &coverageNames);
  }
  const std::string positionName = "transformedPosition";
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(positionName, SLType::Float2));

  // The IR route: the transformed position, the edge varying and the coverage call as SSA values.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    emitVaryingIR(args, edgeVarying, ShaderVar(edgeDistance));
    emitPositionTransformIR(ir, position.name(), SLType::Float2, matrixName);
    ir->SetStage(ShaderStage::Fragment);
    emitOutputColorIR(ir, colorName, false, args.outputColor);
    const auto edgeValue =
        ir->InternValue(edgeVarying.fsIn(), SLType::Float, Precision::High, IRValueKind::Varying);
    const auto coverageResult = HairlineLineCoverageOp::Emit(
        ir, uniformHandler, &coverageNames, edgeValue, aaType == AAType::Coverage ? 1 : 0);
    emitOutputCoverageIR(ir, coverageResult, false, args.outputCoverage);
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
  HairlineLineCoverageUniforms coverageUniforms = {};
  coverageUniforms.coverageScale = coverage;
  coverageUniforms.upload(fragmentUniformData);
}

}  // namespace tgfx
