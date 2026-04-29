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

  // Pass per-corner radii to fragment shader
  auto xRadiiVarying = varyingHandler->addVarying("xRadii", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", xRadiiVarying.vsOut().c_str(), inXRadii.name().c_str());
  auto yRadiiVarying = varyingHandler->addVarying("yRadii", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", yRadiiVarying.vsOut().c_str(), inYRadii.name().c_str());

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

  // Fragment shader - evaluate round rect shape using SDF with per-corner radii
  fragBuilder->codeAppendf("vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec4 xR = %s;", xRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec4 yR = %s;", yRadiiVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec4 bounds = %s;", boundsVarying.fsIn().c_str());

  // For each corner, test whether the fragment lies in the axis-aligned box spanning
  // from the rect corner to that corner's arc center; ScaleRadii guarantees the four boxes
  // do not overlap, so at most one test fires. Inside a corner box we evaluate that
  // corner's local ellipse; outside all of them the fragment sits on an edge or in the
  // center and is always inside the rect (fill) or needs an inner-rect test (stroke).
  // xR/yR order: [TL, TR, BR, BL].
  fragBuilder->codeAppend("vec4 cornersX = vec4(bounds.x, bounds.z, bounds.z, bounds.x);");
  fragBuilder->codeAppend("vec4 cornersY = vec4(bounds.y, bounds.y, bounds.w, bounds.w);");
  fragBuilder->codeAppend("vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);");
  fragBuilder->codeAppend("vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);");
  fragBuilder->codeAppend("vec4 dx = (vec4(localCoord.x) - cornersX) * signsX;");
  fragBuilder->codeAppend("vec4 dy = (vec4(localCoord.y) - cornersY) * signsY;");
  // dx and dy are always non-negative because the quad covers exactly the (possibly
  // stroke-outset) rect, so the lower bound checks are omitted.
  fragBuilder->codeAppend("vec4 inBox = step(dx, xR) * step(dy, yR);");
  fragBuilder->codeAppend("float inAnyCorner = clamp(dot(inBox, vec4(1.0)), 0.0, 1.0);");
  // Selected corner's arc center and radii (in local coordinates).
  fragBuilder->codeAppend("vec4 arcCentersX = cornersX + signsX * xR;");
  fragBuilder->codeAppend("vec4 arcCentersY = cornersY + signsY * yR;");
  fragBuilder->codeAppend(
      "vec2 arcCenter = vec2(dot(inBox, arcCentersX), dot(inBox, arcCentersY));");
  fragBuilder->codeAppend("vec2 radii = max(vec2(dot(inBox, xR), dot(inBox, yR)), vec2(1e-6));");

  // Outer coverage: inside the owning corner ellipse, or anywhere outside all corner boxes
  // (edge/center region).
  fragBuilder->codeAppend("vec2 ellipseOffset = (localCoord - arcCenter) / radii;");
  fragBuilder->codeAppend("float insideEllipse = step(dot(ellipseOffset, ellipseOffset), 1.0);");
  fragBuilder->codeAppend("float outerCoverage = mix(1.0, insideEllipse, inAnyCorner);");

  if (stroke) {
    fragBuilder->codeAppendf("vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
    // Inner coverage: for corner fragments, test the inner ellipse (same center, radii
    // shrunk by the full stroke width); for edge/center fragments, test the inner rect
    // (rect shrunk by the full stroke width on each side).
    fragBuilder->codeAppend("vec2 innerRadii = max(radii - 2.0 * sw, vec2(1e-6));");
    fragBuilder->codeAppend("vec2 innerEllipseOffset = (localCoord - arcCenter) / innerRadii;");
    fragBuilder->codeAppend(
        "float insideInnerEllipse = step(dot(innerEllipseOffset, innerEllipseOffset), 1.0);");
    fragBuilder->codeAppend("vec2 center = (bounds.xy + bounds.zw) * 0.5;");
    fragBuilder->codeAppend("vec2 halfSize = (bounds.zw - bounds.xy) * 0.5;");
    fragBuilder->codeAppend("vec2 innerHalfSize = halfSize - 2.0 * sw;");
    fragBuilder->codeAppend("vec2 p = abs(localCoord - center);");
    fragBuilder->codeAppend(
        "float insideInnerRect = step(0.0, innerHalfSize.x) * step(0.0, innerHalfSize.y)"
        " * step(p.x, innerHalfSize.x) * step(p.y, innerHalfSize.y);");
    fragBuilder->codeAppend(
        "float innerCoverage = mix(insideInnerRect, insideInnerEllipse, inAnyCorner);");
    fragBuilder->codeAppend("float coverage = outerCoverage * (1.0 - innerCoverage);");
  } else {
    fragBuilder->codeAppend("float coverage = outerCoverage;");
  }

  fragBuilder->codeAppendf("%s = vec4(coverage);", args.outputCoverage.c_str());
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
