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

#include "GLSLMeshGeometryProcessor.h"
#include "gpu/VertexShaderBuilder.h"

namespace tgfx {

PlacementPtr<MeshGeometryProcessor> MeshGeometryProcessor::Make(BlockAllocator* allocator,
                                                                bool hasTexCoords, bool hasColors,
                                                                bool hasCoverage, PMColor color,
                                                                const Matrix& viewMatrix) {
  return allocator->make<GLSLMeshGeometryProcessor>(hasTexCoords, hasColors, hasCoverage, color,
                                                    viewMatrix);
}

GLSLMeshGeometryProcessor::GLSLMeshGeometryProcessor(bool hasTexCoords, bool hasColors,
                                                     bool hasCoverage, PMColor color,
                                                     const Matrix& viewMatrix)
    : MeshGeometryProcessor(hasTexCoords, hasColors, hasCoverage, color, viewMatrix) {
}

void GLSLMeshGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  const std::string positionName = "position";

  // Handle texture coordinates for FragmentProcessor
  Varying texCoordVar = {};
  if (hasTexCoords) {
    // User provided texCoords, pass them through
    texCoordVar = varyingHandler->addVarying("TexCoord", SLType::Float2);
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(texCoord));
  } else {
    // No user texCoords, use position with uvMatrix (like DefaultGeometryProcessor)
    emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  }

  std::string colorUniformName = {};
  Varying colorVar = {};
  if (hasColors) {
    colorVar = varyingHandler->addVarying("Color", SLType::Float4);
  } else {
    colorUniformName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  }

  // Handle coverage for anti-aliasing
  Varying coverageVar = {};
  if (hasCoverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
  } else {
  }

  // The IR route: the transformed position and every optional varying as SSA values.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    if (hasTexCoords) {
      emitVaryingIR(args, texCoordVar, ShaderVar(texCoord));
    }
    if (hasColors) {
      emitVaryingIR(args, colorVar, ShaderVar(color));
    }
    if (hasCoverage) {
      emitVaryingIR(args, coverageVar, ShaderVar(coverage));
    }
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
    if (hasColors) {
      const auto colorValue = ir->InternValue(colorVar.fsIn(), SLType::Float4, Precision::High,
                                              IRValueKind::Varying);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    } else {
      const auto colorValue =
          ir->InternValue(colorUniformName, SLType::Float4, Precision::High, IRValueKind::Uniform);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    }
    if (hasCoverage) {
      const auto coverageIn = ir->InternValue(coverageVar.fsIn(), SLType::Float, Precision::High,
                                              IRValueKind::Varying);
      const auto coverage4 = ir->CreateConstruct(
          SLType::Float4, {coverageIn, coverageIn, coverageIn, coverageIn});
      const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                  Precision::High, IRValueKind::Local);
      ir->StoreOutput(coverageOutput, coverage4);
    } else {
      const auto coverage4 = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
      const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                  Precision::High, IRValueKind::Local);
      ir->StoreOutput(coverageOutput, coverage4);
    }
  }
}

void GLSLMeshGeometryProcessor::setData(UniformData* vertexUniformData,
                                        UniformData* fragmentUniformData,
                                        FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("Matrix", viewMatrix);

  // For mesh, position is already in local coordinate space (not pre-transformed like shape).
  // Both hasTexCoords and !hasTexCoords cases use identity since coordinates don't need
  // additional transformation - they're already suitable for shader sampling.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);

  if (!hasColors) {
    fragmentUniformData->setData("Color", commonColor);
  }
}

}  // namespace tgfx
