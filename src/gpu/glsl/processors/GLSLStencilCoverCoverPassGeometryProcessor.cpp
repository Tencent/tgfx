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

#include "gpu/ir/ProgramIREmit.h"
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
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  // Brush colour uniform — matches GLSLDefaultGeometryProcessor. Without this uniform the
  // cover pass would emit a hard-coded colour (white) and silently ignore the brush.
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);

  // The IR route: the transformed position, the constant coverage and the brush colour as SSA
  // values, so the SPIR-V path serves this geometry without the text emission.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    emitPositionTransformIR(ir, position.name(), SLType::Float2, matrixName);
    const auto one = ir->CreateConstant(1.0f);
    ir->SetStage(ShaderStage::Fragment);
    emitOutputColorIR(ir, colorName, false, args.outputColor);
    emitOutputCoverageIR(ir, one, false, args.outputCoverage);
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
