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

#include "GLSLComplexNonAARRectGeometryProcessor.h"
#include "GeneratedOperators.h"

namespace tgfx {
PlacementPtr<ComplexNonAARRectGeometryProcessor> ComplexNonAARRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLComplexNonAARRectGeometryProcessor>(width, height, stroke,
                                                                 commonColor);
}

GLSLComplexNonAARRectGeometryProcessor::GLSLComplexNonAARRectGeometryProcessor(
    int width, int height, bool stroke, std::optional<PMColor> commonColor)
    : ComplexNonAARRectGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLComplexNonAARRectGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit attributes

  // Setup color output
  std::string colorUniformName = {};
  Varying colorVarying = {};
  if (commonColor.has_value()) {
    colorUniformName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorUniformName);
  } else {
    colorVarying = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->storeVarying(colorVarying.vsOut(), inColor.name());
    fragBuilder->storeColor(args.outputColor, colorVarying.fsIn());
  }

  // Output position using RTAdjust uniform
  vertBuilder->storePosition(inPosition.name());

  // Pass local coordinates to fragment shader
  auto localCoordVarying = varyingHandler->addVarying("localCoord", SLType::Float2);
  vertBuilder->storeVarying(localCoordVarying.vsOut(), inLocalCoord.name());

  // Pass per-corner radii to fragment shader
  auto xRadiiVarying = varyingHandler->addVarying("xRadii", SLType::Float4);
  vertBuilder->storeVarying(xRadiiVarying.vsOut(), inXRadii.name());
  auto yRadiiVarying = varyingHandler->addVarying("yRadii", SLType::Float4);
  vertBuilder->storeVarying(yRadiiVarying.vsOut(), inYRadii.name());

  // Pass rect bounds to fragment shader
  auto boundsVarying = varyingHandler->addVarying("rectBounds", SLType::Float4);
  vertBuilder->storeVarying(boundsVarying.vsOut(), inRectBounds.name());

  // Pass stroke width to fragment shader (stroke mode only)
  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("strokeWidth", SLType::Float2);
    vertBuilder->storeVarying(strokeWidthVarying.vsOut(), inStrokeWidth.name());
  }

  // Emit transforms using position as UV coordinates.
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(inPosition.name(), SLType::Float2));

  // The IR route: the same varyings and the coverage call as SSA values, so the SPIR-V path
  // serves this geometry without the text emission.
  if (args.irBuilder != nullptr) {
    auto ir = args.irBuilder;
    if (!commonColor.has_value()) {
      emitVaryingIR(args, colorVarying, ShaderVar(inColor));
    }
    emitPositionIR(args, ShaderVar(inPosition));
    emitVaryingIR(args, localCoordVarying, ShaderVar(inLocalCoord));
    emitVaryingIR(args, xRadiiVarying, ShaderVar(inXRadii));
    emitVaryingIR(args, yRadiiVarying, ShaderVar(inYRadii));
    emitVaryingIR(args, boundsVarying, ShaderVar(inRectBounds));
    if (stroke) {
      emitVaryingIR(args, strokeWidthVarying, ShaderVar(inStrokeWidth));
    }
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
    const auto localCoordIn = ir->InternValue(localCoordVarying.fsIn(), SLType::Float2,
                                              Precision::High, IRValueKind::Varying);
    const auto boundsIn = ir->InternValue(boundsVarying.fsIn(), SLType::Float4, Precision::High,
                                          IRValueKind::Varying);
    const auto xRadiiIn = ir->InternValue(xRadiiVarying.fsIn(), SLType::Float4, Precision::High,
                                          IRValueKind::Varying);
    const auto yRadiiIn = ir->InternValue(yRadiiVarying.fsIn(), SLType::Float4, Precision::High,
                                          IRValueKind::Varying);
    ValueID strokeWidthIn = INVALID_VALUE_ID;
    if (stroke) {
      strokeWidthIn = ir->InternValue(strokeWidthVarying.fsIn(), SLType::Float2, Precision::High,
                                      IRValueKind::Varying);
    } else {
      const auto zero = ir->CreateConstant(0.0f);
      strokeWidthIn = ir->CreateConstruct(SLType::Float2, {zero, zero});
    }
    const auto coverageValue =
        ComplexRRectCoverageOp::Emit(ir, uniformHandler, nullptr, localCoordIn, boundsIn, xRadiiIn,
                                      yRadiiIn, strokeWidthIn, stroke ? 1 : 0);
    const auto coverage4 = ir->CreateConstruct(
        SLType::Float4, {coverageValue, coverageValue, coverageValue, coverageValue});
    const auto coverageOutput = ir->InternValue(args.outputCoverage, SLType::Float4,
                                                Precision::High, IRValueKind::Local);
    ir->StoreOutput(coverageOutput, coverage4);
  }

  // Fragment shader - evaluate the per-corner round rect through the shared SDF. The stroke form
  // rides the function's runtime flag as a literal, so the driver folds the arm this draw does
  // not use.
  fragBuilder->aliasValue("localCoord", localCoordVarying.fsIn(), "highp vec2");
  fragBuilder->aliasValue("xR", xRadiiVarying.fsIn(), "highp vec4");
  fragBuilder->aliasValue("yR", yRadiiVarying.fsIn(), "highp vec4");
  fragBuilder->aliasValue("bounds", boundsVarying.fsIn(), "highp vec4");
  auto coverageEntry = fragBuilder->appendFunction("complexRRectCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (stroke) {
    fragBuilder->aliasValue("sw", strokeWidthVarying.fsIn(), "highp vec2");
    fragBuilder->appendCall(args.outputCoverage, coverageEntry,
                            {"localCoord", "bounds", "xR", "yR", "sw", "1"}, "vec4");
  } else {
    fragBuilder->appendCall(args.outputCoverage, coverageEntry,
                            {"localCoord", "bounds", "xR", "yR", "vec2(0.0)", "0"}, "vec4");
  }
}

void GLSLComplexNonAARRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                                     UniformData* fragmentUniformData,
                                                     FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
