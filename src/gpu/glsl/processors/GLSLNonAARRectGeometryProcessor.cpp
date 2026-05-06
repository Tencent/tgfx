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
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), color.fsIn().c_str());
  }

  // Output position using RTAdjust uniform
  vertBuilder->emitNormalizedPosition(inPosition.name());

  // Pass local coordinates to fragment shader
  auto localCoordVarying = varyingHandler->addVarying("localCoord", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", localCoordVarying.vsOut().c_str(),
                           inLocalCoord.name().c_str());

  // Pass radii to fragment shader
  auto radiiVarying = varyingHandler->addVarying("radii", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", radiiVarying.vsOut().c_str(), inRadii.name().c_str());

  // Pass rect bounds to fragment shader
  auto boundsVarying = varyingHandler->addVarying("rectBounds", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", boundsVarying.vsOut().c_str(), inRectBounds.name().c_str());

  // Pass stroke width to fragment shader (stroke mode only)
  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("strokeWidth", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", strokeWidthVarying.vsOut().c_str(),
                             inStrokeWidth.name().c_str());
  }

  // Emit transforms using position as UV coordinates.
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(inPosition.name(), SLType::Float2));

  // Fragment shader - evaluate round rect shape using SDF
  fragBuilder->codeAppendf("vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec2 radii = %s;", radiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec4 bounds = %s;", boundsVarying.fsIn().c_str());

  // Calculate outer round rect coverage using SDF
  fragBuilder->codeAppend("vec2 center = (bounds.xy + bounds.zw) * 0.5;");
  fragBuilder->codeAppend("vec2 halfSize = (bounds.zw - bounds.xy) * 0.5;");
  fragBuilder->codeAppend("vec2 q = abs(localCoord - center) - halfSize + radii;");
  fragBuilder->codeAppend(
      "float d = min(max(q.x / radii.x, q.y / radii.y), 0.0) + length(max(q / radii, 0.0)) - 1.0;");
  fragBuilder->codeAppend("float outerCoverage = step(d, 0.0);");

  if (stroke) {
    // Stroke mode: also check inner round rect using SDF
    fragBuilder->codeAppendf("vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
    fragBuilder->codeAppend("vec2 innerHalfSize = halfSize - 2.0 * sw;");
    fragBuilder->codeAppend("vec2 innerRadii = max(radii - 2.0 * sw, vec2(0.0));");
    fragBuilder->codeAppend("float innerCoverage = 0.0;");
    // Check if inner rect is valid (not degenerate)
    fragBuilder->codeAppend("if (innerHalfSize.x > 0.0 && innerHalfSize.y > 0.0) {");
    fragBuilder->codeAppend("  vec2 qi = abs(localCoord - center) - innerHalfSize + innerRadii;");
    // Use safe division for inner radii (avoid division by zero)
    fragBuilder->codeAppend("  vec2 safeInnerRadii = max(innerRadii, vec2(0.001));");
    fragBuilder->codeAppend(
        "  float di = min(max(qi.x / safeInnerRadii.x, qi.y / safeInnerRadii.y), 0.0) + "
        "length(max(qi / safeInnerRadii, vec2(0.0))) - 1.0;");
    fragBuilder->codeAppend("  innerCoverage = step(di, 0.0);");
    fragBuilder->codeAppend("}");

    // Final coverage: inside outer but outside inner
    fragBuilder->codeAppend("float coverage = outerCoverage * (1.0 - innerCoverage);");
  } else {
    // Fill mode: just use outer coverage
    fragBuilder->codeAppend("float coverage = outerCoverage;");
  }

  fragBuilder->codeAppendf("%s = vec4(coverage);", args.outputCoverage.c_str());
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
