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

#include "GLSLEllipseGeometryProcessor.h"
#include "GeneratedOperators.h"

namespace tgfx {
PlacementPtr<EllipseGeometryProcessor> EllipseGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLEllipseGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLEllipseGeometryProcessor::GLSLEllipseGeometryProcessor(int width, int height, bool stroke,
                                                           std::optional<PMColor> commonColor)
    : EllipseGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLEllipseGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // emit attributes

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);

  auto fragBuilder = args.fragBuilder;
  // setup pass through color
  std::string colorUniformName = {};
  Varying colorVarying = {};
  if (commonColor.has_value()) {
    colorUniformName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  } else {
    colorVarying = varyingHandler->addVarying("Color", SLType::Float4);
  }

  // Setup position
  // emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));

  // The IR route: the same varyings and the coverage call as SSA values, so the SPIR-V path
  // serves this geometry without the text emission.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    emitVaryingIR(args, ellipseOffsets, ShaderVar(inEllipseOffset));
    emitVaryingIR(args, ellipseRadii, ShaderVar(inEllipseRadii));
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
    const auto coverageValue =
        EllipseCoverageOp::Emit(ir, uniformHandler, nullptr, offsetsIn, radiiIn, stroke ? 1 : 0);
    const auto coverage4 = ir->CreateConstruct(
        SLType::Float4, {coverageValue, coverageValue, coverageValue, coverageValue});
    const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                Precision::High, IRValueKind::Local);
    ir->StoreOutput(coverageOutput, coverage4);
  }
  // For stroked ellipses, we use the full ellipse equation (x^2/a^2 + y^2/b^2 = 1)
  // to compute both the edges because we need two separate test equations for
  // the single offset.
  // For filled ellipses we can use a unit circle equation (x^2 + y^2 = 1), and warp
  // the distance by the gradient, non-uniformly scaled by the inverse of the
  // ellipse size.

  // On medium precision devices, we scale the denominator of the distance equation
  // before taking the inverse square root to minimize the chance that we're dividing
  // by zero, then we scale the result back.

  // Fragment shader: the shared ellipse edge coverage. The stroke form rides the function's
  // runtime flag as a literal, so the driver folds the arm this draw does not use.
  auto coverageEntry = fragBuilder->appendFunction("ellipseEdgeCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  fragBuilder->appendCall(
      args.outputCoverage, coverageEntry,
      {ellipseOffsets.fsIn() + ".xy", ellipseRadii.fsIn(), std::to_string(stroke ? 1 : 0)}, "vec4");
}

void GLSLEllipseGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
