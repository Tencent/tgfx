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

#include "GLSLNonAARRectGeometryProcessor.h"

namespace tgfx {
PlacementPtr<NonAARRectGeometryProcessor> NonAARRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLNonAARRectGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLNonAARRectGeometryProcessor::GLSLNonAARRectGeometryProcessor(int width, int height, bool stroke,
                                                                 std::optional<PMColor> commonColor)
    : NonAARRectGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLNonAARRectGeometryProcessor::emitCode(EmitArgs& args) const {
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

  // Pass radii to fragment shader
  auto radiiVarying = varyingHandler->addVarying("radii", SLType::Float2);
  vertBuilder->storeVarying(radiiVarying.vsOut(), inRadii.name());

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

  // Fragment shader - evaluate round rect shape using the shared SDF. The stroke form rides the
  // function's runtime flag as a literal, so the driver folds the arm this draw does not use.
  fragBuilder->aliasValue("localCoord", localCoordVarying.fsIn(), "highp vec2");
  fragBuilder->aliasValue("radii", radiiVarying.fsIn(), "highp vec2");
  fragBuilder->aliasValue("bounds", boundsVarying.fsIn(), "highp vec4");
  auto coverageEntry = fragBuilder->appendFunction("nonAARRectCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (stroke) {
    fragBuilder->aliasValue("sw", strokeWidthVarying.fsIn(), "highp vec2");
    fragBuilder->appendCall(args.outputCoverage, coverageEntry,
                            {"localCoord", "bounds", "radii", "sw", "1"}, "vec4");
  } else {
    fragBuilder->appendCall(args.outputCoverage, coverageEntry,
                            {"localCoord", "bounds", "radii", "vec2(0.0)", "0"}, "vec4");
  }
}

void GLSLNonAARRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                              UniformData* fragmentUniformData,
                                              FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
