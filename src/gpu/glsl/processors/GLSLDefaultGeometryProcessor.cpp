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
#include "gpu/ir/ProgramIRBuilder.h"

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
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto matrixName =
      args.uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->transformPoint(positionName, position.name(), matrixName, false);

  if (args.irBuilder != nullptr) {
    const auto matrixValue = args.irBuilder->InternValue(matrixName, SLType::Float3x3,
                                                         Precision::High, IRValueKind::Uniform);
    const auto positionValue = args.irBuilder->InternValue(position.name(), SLType::Float2,
                                                           Precision::High, IRValueKind::Attribute);
    const auto one = args.irBuilder->CreateConstant(1.0f);
    const auto position3 = args.irBuilder->CreateConstruct(SLType::Float3, {positionValue, one});
    const auto transformed =
        args.irBuilder->CreateBinary(IRBinaryOp::Mul, SLType::Float3, matrixValue, position3);
    const auto position4 = args.irBuilder->CreateConstruct(SLType::Float4, {transformed, one});
    const auto output = args.irBuilder->CreateValue(SLType::Float4, Precision::High,
                                                    IRValueKind::Local, "gl_Position");
    args.irBuilder->StoreOutput(output, position4);
  }

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->storeVarying(coverageVar.vsOut(), coverage.name());
    fragBuilder->storeCoverage(args.outputCoverage, coverageVar.fsIn(), true);
    if (args.irBuilder != nullptr) {
      const auto coverageVarying =
          args.irBuilder->AddVarying({coverageVar.name(), coverageVar.type(), Precision::High, 0});
      const auto coverageValue = args.irBuilder->InternValue(
          coverage.name(), SLType::Float, Precision::High, IRValueKind::Attribute);
      args.irBuilder->StoreVarying({coverageVarying, ""}, coverageValue);
      args.irBuilder->SetStage(ShaderStage::Fragment);
      const auto coverageInput = args.irBuilder->InternValue(coverageVar.fsIn(), SLType::Float,
                                                             Precision::High, IRValueKind::Varying);
      const auto coverage4 = args.irBuilder->CreateConstruct(
          SLType::Float4, {coverageInput, coverageInput, coverageInput, coverageInput});
      const auto coverageOutput = args.irBuilder->CreateValue(
          SLType::Float4, Precision::High, IRValueKind::Local, args.outputCoverage);
      args.irBuilder->Assign({coverageOutput, ""}, coverage4);
    }
  } else {
    fragBuilder->storeCoverage(args.outputCoverage, "1.0", true);
    if (args.irBuilder != nullptr) {
      args.irBuilder->SetStage(ShaderStage::Fragment);
      const auto one = args.irBuilder->CreateConstant(1.0f);
      const auto coverage4 = args.irBuilder->CreateConstruct(SLType::Float4, {one, one, one, one});
      const auto coverageOutput = args.irBuilder->CreateValue(
          SLType::Float4, Precision::High, IRValueKind::Local, args.outputCoverage);
      args.irBuilder->Assign({coverageOutput, ""}, coverage4);
    }
  }

  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->storeColor(args.outputColor, colorName);
  if (args.irBuilder != nullptr) {
    args.irBuilder->SetStage(ShaderStage::Fragment);
    const auto colorValue = args.irBuilder->InternValue(colorName, SLType::Float4, Precision::High,
                                                        IRValueKind::Uniform);
    const auto colorOutput = args.irBuilder->InternValue(args.outputColor, SLType::Float4,
                                                         Precision::High, IRValueKind::Local);
    args.irBuilder->StoreOutput(colorOutput, colorValue);
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->storePosition(positionName);
}

void GLSLDefaultGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix, vertexUniformData, transformIter);
  fragmentUniformData->setData("Color", color);
  vertexUniformData->setData("Matrix", viewMatrix);
}
}  // namespace tgfx
