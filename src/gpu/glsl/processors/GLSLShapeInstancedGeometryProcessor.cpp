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

#include "GLSLShapeInstancedGeometryProcessor.h"
#include "gpu/VertexShaderBuilder.h"

namespace tgfx {
PlacementPtr<ShapeInstancedGeometryProcessor> ShapeInstancedGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, AAType aa, bool hasColors,
    const Matrix& uvMatrix, const Matrix& stateMatrix) {
  return allocator->make<GLSLShapeInstancedGeometryProcessor>(width, height, aa, hasColors,
                                                              uvMatrix, stateMatrix);
}

GLSLShapeInstancedGeometryProcessor::GLSLShapeInstancedGeometryProcessor(int width, int height,
                                                                         AAType aa, bool hasColors,
                                                                         const Matrix& uvMatrix,
                                                                         const Matrix& stateMatrix)
    : ShapeInstancedGeometryProcessor(width, height, aa, hasColors, uvMatrix, stateMatrix) {
}

void GLSLShapeInstancedGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  auto uvMatrixName =
      uniformHandler->addUniform("UVMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  auto viewMatrixName =
      uniformHandler->addUniform("ViewMatrix", UniformFormat::Float3x3, ShaderStage::Vertex);

  // UV transforms use the unshifted local coords ('local'), computed in the IR block below: the
  // mask texture and shader patterns are local-space, only the device position takes the offset.
  ShaderVar localVar("local", SLType::Float2);
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, localVar);

  Varying coverageVar = {};
  if (aa == AAType::Coverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
  }

  // Color: per-instance color or opaque white (overridden by shader FP).
  Varying colorVar = {};
  if (hasColors) {
    colorVar = varyingHandler->addVarying("InstanceColor", SLType::Float4);
  }

  // The IR route: both matrix steps (local and the offset-shifted device position) and every
  // optional varying as SSA values. The local value feeds the coordinate transforms the same
  // emitTransforms call above already registered; its name is what the transform walk interns.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    const auto one = ir->CreateConstant(1.0f);
    const auto zero = ir->CreateConstant(0.0f);
    const auto positionValue = ir->InternValue(position.name(), SLType::Float2, Precision::High,
                                               IRValueKind::Attribute);
    const auto position3 = ir->CreateConstruct(SLType::Float3, {positionValue, one});
    // local = uvMatrix * position (the coordinate-transform source, in local space).
    const auto uvMatrixValue =
        ir->InternValue(uvMatrixName, SLType::Float3x3, Precision::High, IRValueKind::Uniform);
    const auto localTrans =
        ir->CreateBinary(IRBinaryOp::Mul, SLType::Float3, uvMatrixValue, position3);
    const auto localXY = ir->CreateSwizzle(localTrans, SLType::Float2, "xy");
    const auto local = ir->CreateValue(SLType::Float2, Precision::High, IRValueKind::Local,
                                       "local");
    ir->Assign({local, ""}, localXY);
    if (aa == AAType::Coverage) {
      emitVaryingIR(args, coverageVar, ShaderVar(coverage));
    }
    if (hasColors) {
      emitVaryingIR(args, colorVar, ShaderVar(instanceColor));
    }
    // device position = viewMatrix * position + offset.
    const auto viewMatrixValue =
        ir->InternValue(viewMatrixName, SLType::Float3x3, Precision::High, IRValueKind::Uniform);
    const auto deviceTrans =
        ir->CreateBinary(IRBinaryOp::Mul, SLType::Float3, viewMatrixValue, position3);
    const auto deviceXY = ir->CreateSwizzle(deviceTrans, SLType::Float2, "xy");
    const auto offsetValue = ir->InternValue(offset.name(), SLType::Float2, Precision::High,
                                             IRValueKind::Attribute);
    const auto shifted = ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, deviceXY, offsetValue);
    const auto rtAdjust =
        ir->InternValue(RTAdjustName, SLType::Float4, Precision::High, IRValueKind::Uniform);
    const auto adjustXZ = ir->CreateSwizzle(rtAdjust, SLType::Float2, "xz");
    const auto adjustYW = ir->CreateSwizzle(rtAdjust, SLType::Float2, "yw");
    const auto scaled = ir->CreateBinary(IRBinaryOp::Mul, SLType::Float2, shifted, adjustXZ);
    const auto shiftedAdjust =
        ir->CreateBinary(IRBinaryOp::Add, SLType::Float2, scaled, adjustYW);
    const auto position4 = ir->CreateConstruct(SLType::Float4, {shiftedAdjust, zero, one});
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
      const auto color4 = ir->CreateConstruct(SLType::Float4, {one, one, one, one});
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, color4);
    }
    if (aa == AAType::Coverage) {
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

void GLSLShapeInstancedGeometryProcessor::setData(UniformData* vertexUniformData, UniformData*,
                                                  FPCoordTransformIter* transformIter) const {
  vertexUniformData->setData("UVMatrix", uvMatrix);
  vertexUniformData->setData("ViewMatrix", viewMatrix);
  // For instanced rendering, emitTransforms uses local coords directly. The coord transform
  // matrices only need the FP's own transform since uvMatrix already recovers local coords.
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
}
}  // namespace tgfx
