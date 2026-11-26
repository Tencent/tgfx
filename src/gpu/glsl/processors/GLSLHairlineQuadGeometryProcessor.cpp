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

#include "GLSLHairlineQuadGeometryProcessor.h"
#include "GeneratedOperators.h"
#include "gpu/VertexShaderBuilder.h"
#include "tgfx/core/Color.h"

namespace tgfx {

PlacementPtr<HairlineQuadGeometryProcessor> HairlineQuadGeometryProcessor::Make(
    BlockAllocator* allocator, const PMColor& color, const Matrix& viewMatrix,
    std::optional<Matrix> uvMatrix, float coverage, AAType aaType) {
  return allocator->make<GLSLHairlineQuadGeometryProcessor>(color, viewMatrix, uvMatrix, coverage,
                                                            aaType);
}

GLSLHairlineQuadGeometryProcessor::GLSLHairlineQuadGeometryProcessor(const PMColor& color,
                                                                     const Matrix& viewMatrix,
                                                                     std::optional<Matrix> uvMatrix,
                                                                     float coverage, AAType aaType)
    : HairlineQuadGeometryProcessor(color, viewMatrix, uvMatrix, coverage, aaType) {
}

void GLSLHairlineQuadGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  auto colorName = uniformHandler->addUniform("Color", UniformFormat::Float4,
                                              ShaderStage::Fragment);
  auto edgeVarying = varyingHandler->addVarying("HairQuadEdge", SLType::Float4);
  HairlineQuadCoverageOp::UniformNames coverageNames = {};
  if (args.irBuilder != nullptr) {
    HairlineQuadCoverageOp::EnsureUniforms(args.irBuilder, uniformHandler, &coverageNames);
  }
  const std::string positionName = "transformedPosition";
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(positionName, SLType::Float2));

  // The IR route: the transformed position, the edge varying and the coverage call as SSA values.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    emitVaryingIR(args, edgeVarying, ShaderVar(hairQuadEdge));
    const auto matrixValue = ir->InternValue(matrixName, SLType::Float3x3, Precision::High,
                                             IRValueKind::Uniform);
    const auto positionValue = ir->InternValue(position.name(), SLType::Float2, Precision::High,
                                               IRValueKind::Attribute);
    const auto one = ir->CreateConstant(1.0f);
    const auto zero = ir->CreateConstant(0.0f);
    const auto position3 = ir->CreateConstruct(SLType::Float3, {positionValue, one});
    const auto transformed =
        ir->CreateBinary(IRBinaryOp::Mul, SLType::Float3, matrixValue, position3);
    const auto transformedXY = ir->CreateSwizzle(transformed, SLType::Float2, "xy");
    const auto rtAdjust =
        ir->InternValue(RTAdjustName, SLType::Float4, Precision::High, IRValueKind::Uniform);
    const auto adjustXZ = ir->CreateSwizzle(rtAdjust, SLType::Float2, "xz");
    const auto adjustYW = ir->CreateSwizzle(rtAdjust, SLType::Float2, "yw");
    const auto scaled =
        ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, transformedXY, adjustXZ);
    const auto shifted = ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, scaled, adjustYW);
    const auto position4 = ir->CreateConstruct(SLType::Float4, {shifted, zero, one});
    const auto positionOutput =
        ir->CreateValue(SLType::Float4, Precision::High, IRValueKind::Local, "gl_Position");
    ir->StoreOutput(positionOutput, position4);
    ir->SetStage(ShaderStage::Fragment);
    const auto colorValue =
        ir->InternValue(colorName, SLType::Float4, Precision::High, IRValueKind::Uniform);
    const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                             IRValueKind::Local);
    ir->StoreOutput(colorOutput, colorValue);
    const auto edgeValue =
        ir->InternValue(edgeVarying.fsIn(), SLType::Float4, Precision::High, IRValueKind::Varying);
    const auto coverageResult = HairlineQuadCoverageOp::Emit(
        ir, uniformHandler, &coverageNames, edgeValue, aaType == AAType::Coverage ? 1 : 0);
    const auto coverage4 = ir->CreateConstruct(
        SLType::Float4, {coverageResult, coverageResult, coverageResult, coverageResult});
    const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                Precision::High, IRValueKind::Local);
    ir->StoreOutput(coverageOutput, coverage4);
  }
}

void GLSLHairlineQuadGeometryProcessor::setData(UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData,
                                                FPCoordTransformIter* transformIter) const {
  if (uvMatrix.has_value()) {
    setTransformDataHelper(*uvMatrix, vertexUniformData, transformIter);
  }
  fragmentUniformData->setData("Color", color);
  vertexUniformData->setData("Matrix", viewMatrix);
  HairlineQuadCoverageUniforms coverageUniforms = {};
  coverageUniforms.coverageScale = coverage;
  coverageUniforms.upload(fragmentUniformData);
}

}  // namespace tgfx
