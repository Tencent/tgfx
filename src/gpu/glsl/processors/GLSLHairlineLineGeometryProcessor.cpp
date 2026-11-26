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
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit vertex attributes

  // Transform vertex position by view matrix
  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "transformedPosition";
  vertBuilder->transformPoint(positionName, position.name(), matrixName, false);

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(positionName, SLType::Float2));
  // Pass edge distance to fragment shader for anti-aliasing
  auto edgeVarying = varyingHandler->addVarying("EdgeDistance", SLType::Float);
  vertBuilder->storeVarying(edgeVarying.vsOut(), edgeDistance.name());

  // Fragment shader: the shared hairline edge alpha. The non-coverage step rides the function's
  // runtime flag as a literal, so the driver folds the arm this draw does not use.
  auto coverageEntry = fragBuilder->appendFunction("hairlineLineEdgeCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->storeColor(args.outputColor, colorName);
  HairlineLineCoverageOp::UniformNames coverageNames = {};
  if (args.irBuilder != nullptr) {
    HairlineLineCoverageOp::EnsureUniforms(args.irBuilder, uniformHandler, &coverageNames);
  }
  fragBuilder->appendCall(
      args.outputCoverage, coverageEntry,
      {edgeVarying.fsIn(), coverageNames.coverageScale,
       std::to_string(aaType == AAType::Coverage ? 1 : 0)},
      "vec4");

  // Emit final vertex position
  vertBuilder->storePosition(positionName);

  // The IR route: the transformed position, the edge varying and the coverage call as SSA values.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    emitVaryingIR(args, edgeVarying, ShaderVar(edgeDistance));
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
        ir->InternValue(edgeVarying.fsIn(), SLType::Float, Precision::High, IRValueKind::Varying);
    const auto coverageResult = HairlineLineCoverageOp::Emit(
        ir, uniformHandler, &coverageNames, edgeValue, aaType == AAType::Coverage ? 1 : 0);
    const auto coverage4 = ir->CreateConstruct(
        SLType::Float4, {coverageResult, coverageResult, coverageResult, coverageResult});
    const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                Precision::High, IRValueKind::Local);
    ir->StoreOutput(coverageOutput, coverage4);
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
