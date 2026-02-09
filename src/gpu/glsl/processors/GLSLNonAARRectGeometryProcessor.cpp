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
    // In fill mode (no stroke), color attribute is stored in inStrokeWidth slot.
    // In stroke mode, color attribute is stored in inColor slot.
    auto& colorAttr = stroke ? inColor : inStrokeWidth;
    vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), colorAttr.name().c_str());
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

  // Fragment shader - evaluate round rect shape
  fragBuilder->codeAppendf("vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec2 radii = %s;", radiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec4 bounds = %s;", boundsVarying.fsIn().c_str());

  // Calculate distance from corners
  fragBuilder->codeAppend("float left = bounds.x;");
  fragBuilder->codeAppend("float top = bounds.y;");
  fragBuilder->codeAppend("float right = bounds.z;");
  fragBuilder->codeAppend("float bottom = bounds.w;");
  fragBuilder->codeAppend("float rx = radii.x;");
  fragBuilder->codeAppend("float ry = radii.y;");

  // Check if point is inside the outer round rect
  fragBuilder->codeAppend("float outerCoverage = 1.0;");
  fragBuilder->codeAppend("vec2 cornerDist;");

  // Top-left corner
  fragBuilder->codeAppend("if (localCoord.x < left + rx && localCoord.y < top + ry) {");
  fragBuilder->codeAppend("  cornerDist = (localCoord - vec2(left + rx, top + ry)) / radii;");
  fragBuilder->codeAppend("  if (dot(cornerDist, cornerDist) > 1.0) outerCoverage = 0.0;");
  fragBuilder->codeAppend("}");

  // Top-right corner
  fragBuilder->codeAppend("else if (localCoord.x > right - rx && localCoord.y < top + ry) {");
  fragBuilder->codeAppend("  cornerDist = (localCoord - vec2(right - rx, top + ry)) / radii;");
  fragBuilder->codeAppend("  if (dot(cornerDist, cornerDist) > 1.0) outerCoverage = 0.0;");
  fragBuilder->codeAppend("}");

  // Bottom-right corner
  fragBuilder->codeAppend("else if (localCoord.x > right - rx && localCoord.y > bottom - ry) {");
  fragBuilder->codeAppend("  cornerDist = (localCoord - vec2(right - rx, bottom - ry)) / radii;");
  fragBuilder->codeAppend("  if (dot(cornerDist, cornerDist) > 1.0) outerCoverage = 0.0;");
  fragBuilder->codeAppend("}");

  // Bottom-left corner
  fragBuilder->codeAppend("else if (localCoord.x < left + rx && localCoord.y > bottom - ry) {");
  fragBuilder->codeAppend("  cornerDist = (localCoord - vec2(left + rx, bottom - ry)) / radii;");
  fragBuilder->codeAppend("  if (dot(cornerDist, cornerDist) > 1.0) outerCoverage = 0.0;");
  fragBuilder->codeAppend("}");

  if (stroke) {
    // Stroke mode: also check inner round rect
    fragBuilder->codeAppendf("vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
    fragBuilder->codeAppend("float innerLeft = left + sw.x;");
    fragBuilder->codeAppend("float innerTop = top + sw.y;");
    fragBuilder->codeAppend("float innerRight = right - sw.x;");
    fragBuilder->codeAppend("float innerBottom = bottom - sw.y;");
    fragBuilder->codeAppend("float innerRx = max(rx - sw.x, 0.0);");
    fragBuilder->codeAppend("float innerRy = max(ry - sw.y, 0.0);");

    // Check if inner rect is valid (not degenerate)
    fragBuilder->codeAppend("float innerCoverage = 0.0;");
    fragBuilder->codeAppend("if (innerRight > innerLeft && innerBottom > innerTop) {");
    fragBuilder->codeAppend("  innerCoverage = 1.0;");

    // Check if point is inside the inner round rect
    fragBuilder->codeAppend("  if (localCoord.x < innerLeft || localCoord.x > innerRight ||");
    fragBuilder->codeAppend("      localCoord.y < innerTop || localCoord.y > innerBottom) {");
    fragBuilder->codeAppend("    innerCoverage = 0.0;");
    fragBuilder->codeAppend("  }");
    fragBuilder->codeAppend("  else if (innerRx > 0.0 && innerRy > 0.0) {");
    fragBuilder->codeAppend("    vec2 innerRadii = vec2(innerRx, innerRy);");

    // Top-left inner corner
    fragBuilder->codeAppend(
        "    if (localCoord.x < innerLeft + innerRx && localCoord.y < innerTop + innerRy) {");
    fragBuilder->codeAppend(
        "      cornerDist = (localCoord - vec2(innerLeft + innerRx, innerTop + innerRy)) / "
        "innerRadii;");
    fragBuilder->codeAppend("      if (dot(cornerDist, cornerDist) > 1.0) innerCoverage = 0.0;");
    fragBuilder->codeAppend("    }");

    // Top-right inner corner
    fragBuilder->codeAppend(
        "    else if (localCoord.x > innerRight - innerRx && localCoord.y < innerTop + innerRy) {");
    fragBuilder->codeAppend(
        "      cornerDist = (localCoord - vec2(innerRight - innerRx, innerTop + innerRy)) / "
        "innerRadii;");
    fragBuilder->codeAppend("      if (dot(cornerDist, cornerDist) > 1.0) innerCoverage = 0.0;");
    fragBuilder->codeAppend("    }");

    // Bottom-right inner corner
    fragBuilder->codeAppend(
        "    else if (localCoord.x > innerRight - innerRx && localCoord.y > innerBottom - "
        "innerRy) {");
    fragBuilder->codeAppend(
        "      cornerDist = (localCoord - vec2(innerRight - innerRx, innerBottom - innerRy)) / "
        "innerRadii;");
    fragBuilder->codeAppend("      if (dot(cornerDist, cornerDist) > 1.0) innerCoverage = 0.0;");
    fragBuilder->codeAppend("    }");

    // Bottom-left inner corner
    fragBuilder->codeAppend(
        "    else if (localCoord.x < innerLeft + innerRx && localCoord.y > innerBottom - "
        "innerRy) {");
    fragBuilder->codeAppend(
        "      cornerDist = (localCoord - vec2(innerLeft + innerRx, innerBottom - innerRy)) / "
        "innerRadii;");
    fragBuilder->codeAppend("      if (dot(cornerDist, cornerDist) > 1.0) innerCoverage = 0.0;");
    fragBuilder->codeAppend("    }");

    fragBuilder->codeAppend("  }");
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
