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

#include "GLSLStencilCoverCoverPassGeometryProcessor.h"
#include "gpu/VertexShaderBuilder.h"

namespace tgfx {
PlacementPtr<StencilCoverCoverPassGeometryProcessor> StencilCoverCoverPassGeometryProcessor::Make(
    BlockAllocator* allocator, PMColor color, const Matrix& viewMatrix) {
  return allocator->make<GLSLStencilCoverCoverPassGeometryProcessor>(color, viewMatrix);
}

GLSLStencilCoverCoverPassGeometryProcessor::GLSLStencilCoverCoverPassGeometryProcessor(
    PMColor color, const Matrix& viewMatrix)
    : StencilCoverCoverPassGeometryProcessor(color, viewMatrix) {
}

void GLSLStencilCoverCoverPassGeometryProcessor::emitCode(EmitArgs& args) const {
  // Cover pass: forward the device-bounds quad position through the view matrix, emit the uv
  // coordinates for any upstream FP (shader / colour filter / mask filter) supplied by the
  // draw op, and write the brush colour as the fragment output. Upstream FPs in the chain run
  // before this processor and composite against outputColor via the standard ProgramBuilder
  // wiring — when there are no FPs, the brush colour is the final colour.
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->transformPoint(positionName, position.name(), matrixName, false);

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  // Default coverage of 1.0; the FP chain (mask filter, clip) may multiply this further.
  fragBuilder->storeCoverage(args.outputCoverage, "1.0", true);
  // Brush colour uniform — matches GLSLDefaultGeometryProcessor. Without this uniform the
  // cover pass would emit a hard-coded colour (white) and silently ignore the brush.
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->storeColor(args.outputColor, colorName);

  vertBuilder->storePosition(positionName);

  // The IR route: the transformed position, the constant coverage and the brush colour as SSA
  // values, so the SPIR-V path serves this geometry without the text emission.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    const auto matrixValue =
        ir->InternValue(matrixName, SLType::Float3x3, Precision::High, IRValueKind::Uniform);
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
    const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, transformedXY, adjustXZ);
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
    const auto coverage4 = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
    const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                Precision::High, IRValueKind::Local);
    ir->StoreOutput(coverageOutput, coverage4);
  }
}

void GLSLStencilCoverCoverPassGeometryProcessor::setData(
    UniformData* vertexUniformData, UniformData* fragmentUniformData,
    FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  vertexUniformData->setData("Matrix", viewMatrix);
  fragmentUniformData->setData("Color", color);
}
}  // namespace tgfx
