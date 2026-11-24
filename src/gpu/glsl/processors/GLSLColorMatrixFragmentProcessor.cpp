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

#include "GLSLColorMatrixFragmentProcessor.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {
PlacementPtr<ColorMatrixFragmentProcessor> ColorMatrixFragmentProcessor::Make(
    BlockAllocator* allocator, const std::array<float, 20>& matrix) {
  return allocator->make<GLSLColorMatrixFragmentProcessor>(matrix);
}

GLSLColorMatrixFragmentProcessor::GLSLColorMatrixFragmentProcessor(
    const std::array<float, 20>& matrix)
    : ColorMatrixFragmentProcessor(matrix) {
}

void GLSLColorMatrixFragmentProcessor::emitCode(EmitArgs& args) const {
  auto uniformHandler = args.uniformHandler;
  auto matrixUniformName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float4x4, ShaderStage::Fragment);
  auto vectorUniformName =
      uniformHandler->addUniform("Vector", UniformFormat::Float4, ShaderStage::Fragment);

  auto fragBuilder = args.fragBuilder;
  auto entry = fragBuilder->appendFunction("pmApplyColorMatrix");
  if (entry.empty()) {
    return;
  }
  if (args.irBuilder == nullptr || args.inputValue == INVALID_VALUE_ID ||
      args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  const auto matrixValue = args.irBuilder->InternValue(matrixUniformName, SLType::Float4x4,
                                                       Precision::High, IRValueKind::Uniform);
  const auto vectorValue = args.irBuilder->InternValue(vectorUniformName, SLType::Float4,
                                                       Precision::High, IRValueKind::Uniform);
  const auto result = args.irBuilder->CreateCall(OperatorID::ColorMatrix, SLType::Float4,
                                                 {args.inputValue, matrixValue, vectorValue});
  args.irBuilder->StoreOutput(args.outputValue, result);
  std::string expression = {};
  std::string error = {};
  if (result == INVALID_VALUE_ID ||
      !GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, &expression, &error)) {
    args.irBuilder->MarkFailed(error.empty() ? "the color-matrix operator could not be emitted"
                                             : error);
    return;
  }
  fragBuilder->appendCall(
      args.outputColor, entry,
      {args.inputColorValue, ValueRef(matrixUniformName, SLType::Float4x4, ValueOrigin::Uniform),
       ValueRef(vectorUniformName, SLType::Float4, ValueOrigin::Uniform)},
      "", expression);
}

void GLSLColorMatrixFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                                 UniformData* fragmentUniformData) const {
  float m[] = {
      matrix[0], matrix[5], matrix[10], matrix[15], matrix[1], matrix[6], matrix[11], matrix[16],
      matrix[2], matrix[7], matrix[12], matrix[17], matrix[3], matrix[8], matrix[13], matrix[18],
  };
  float vec[] = {
      matrix[4],
      matrix[9],
      matrix[14],
      matrix[19],
  };
  fragmentUniformData->setData("Matrix", m);
  fragmentUniformData->setData("Vector", vec);
}
}  // namespace tgfx
