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

#include "GLSLComplexEllipseGeometryProcessor.h"
#include "GeneratedOperators.h"

namespace tgfx {
PlacementPtr<ComplexEllipseGeometryProcessor> ComplexEllipseGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLComplexEllipseGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLComplexEllipseGeometryProcessor::GLSLComplexEllipseGeometryProcessor(
    int width, int height, bool stroke, std::optional<PMColor> commonColor)
    : ComplexEllipseGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLComplexEllipseGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit attributes

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);

  auto edgeDist = varyingHandler->addVarying("EdgeDist", SLType::Float2);

  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("StrokeWidth", SLType::Float2);
  }

  // Setup pass through color
  std::string colorUniformName = {};
  Varying colorVarying = {};
  if (commonColor.has_value()) {
    colorUniformName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  } else {
    colorVarying = varyingHandler->addVarying("Color", SLType::Float4);
  }

  // Setup position
  // Emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));

  // The IR route: the same varyings and the coverage call as SSA values, so the SPIR-V path
  // serves this geometry without the text emission.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    emitVaryingIR(args, ellipseOffsets, ShaderVar(inEllipseOffset));
    emitVaryingIR(args, ellipseRadii, ShaderVar(inEllipseRadii));
    emitVaryingIR(args, edgeDist, ShaderVar(inEdgeDist));
    if (stroke) {
      emitVaryingIR(args, strokeWidthVarying, ShaderVar(inStrokeWidth));
    }
    if (!commonColor.has_value()) {
      emitVaryingIR(args, colorVarying, ShaderVar(inColor));
    }
    emitPositionIR(args, ShaderVar(inPosition));
    ir->SetStage(ShaderStage::Fragment);
    if (commonColor.has_value()) {
      const auto colorValue = ir->InternValue(colorUniformName, SLType::Float4, Precision::High,
                                              IRValueKind::Uniform);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    } else {
      const auto colorValue = ir->InternValue(colorVarying.fsIn(), SLType::Float4,
                                              Precision::High, IRValueKind::Varying);
      const auto colorOutput = ir->InternValue(args.outputColor, SLType::Float4, Precision::High,
                                               IRValueKind::Local);
      ir->StoreOutput(colorOutput, colorValue);
    }
    const auto offsetsIn = ir->InternValue(ellipseOffsets.fsIn(), SLType::Float2, Precision::High,
                                           IRValueKind::Varying);
    const auto radiiIn = ir->InternValue(ellipseRadii.fsIn(), SLType::Float4, Precision::High,
                                         IRValueKind::Varying);
    const auto edgeDistIn = ir->InternValue(edgeDist.fsIn(), SLType::Float2, Precision::High,
                                            IRValueKind::Varying);
    ValueID strokeWidthIn = INVALID_VALUE_ID;
    if (stroke) {
      strokeWidthIn = ir->InternValue(strokeWidthVarying.fsIn(), SLType::Float2, Precision::High,
                                      IRValueKind::Varying);
    } else {
      const auto zero = ir->CreateConstant(0.0f);
      strokeWidthIn = ir->CreateConstruct(SLType::Float2, {zero, zero});
    }
    const auto coverageValue = ComplexEllipseCoverageOp::Emit(
        ir, uniformHandler, nullptr, offsetsIn, radiiIn, edgeDistIn, strokeWidthIn,
        stroke ? 1 : 0);
    const auto coverage4 = ir->CreateConstruct(
        SLType::Float4, {coverageValue, coverageValue, coverageValue, coverageValue});
    const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                Precision::High, IRValueKind::Local);
    ir->StoreOutput(coverageOutput, coverage4);
  }

  // Fragment shader: the shared offset-based region dispatch. The stroke form rides the
  // function's runtime flag as a literal, so the driver folds the arm this draw does not use.
  if (stroke) {
  } else {
  }
}

void GLSLComplexEllipseGeometryProcessor::setData(UniformData* vertexUniformData,
                                                  UniformData* fragmentUniformData,
                                                  FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
