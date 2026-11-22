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
  varyingHandler->emitAttributes(*this);

  // Setup color output
  if (commonColor.has_value()) {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorName);
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->storeVarying(color.vsOut(), inColor.name());
    fragBuilder->storeColor(args.outputColor, color.fsIn());
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

  // Fragment shader - evaluate the per-corner round rect through the shared SDF. The stroke form
  // rides the function's runtime flag as a literal, so the driver folds the arm this draw does
  // not use.
  fragBuilder->codeAppendf("highp vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 xR = %s;", xRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 yR = %s;", yRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 bounds = %s;", boundsVarying.fsIn().c_str());
  auto coverageEntry = fragBuilder->appendFunction("complexRRectCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (stroke) {
    fragBuilder->codeAppendf("highp vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
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
