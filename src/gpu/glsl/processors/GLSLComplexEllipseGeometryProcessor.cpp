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
  varyingHandler->emitAttributes(*this);

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                           inEllipseOffset.name().c_str());

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(), inEllipseRadii.name().c_str());

  auto edgeDist = varyingHandler->addVarying("EdgeDist", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", edgeDist.vsOut().c_str(), inEdgeDist.name().c_str());

  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("StrokeWidth", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", strokeWidthVarying.vsOut().c_str(),
                             inStrokeWidth.name().c_str());
  }

  auto fragBuilder = args.fragBuilder;
  // Setup pass through color
  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), color.fsIn().c_str());
  }

  // Setup position
  args.vertBuilder->emitNormalizedPosition(inPosition.name());
  // Emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));

  // ── Fragment shader: offset-based region dispatch ──
  //
  // The offset attribute encodes each corner vertex's position relative to its corner ellipse.
  // For fills it's the normalized (x/a, y/b) of the ellipse equation; for strokes it's the raw
  // ellipse-space offset in pixels, normalized later in the fragment shader via the radii
  // reciprocals. For vertices not on any arc (rows/columns along straight edges), the
  // corresponding component is set to exact 0.0. Since IEEE 754 preserves exact zero through
  // linear interpolation (0*w₁ + 0*w₂ = 0), the fragment shader can reliably use `offset > 0.0`
  // to distinguish regions:
  //   - Corner region (both offset.x > 0 and offset.y > 0): use ellipse distance
  //   - Horizontal edge region (only offset.y > 0): use edgeDist.y linear AA
  //   - Vertical edge region (only offset.x > 0): use edgeDist.x linear AA
  //   - Center region (both ≤ 0): full coverage
  //
  // In the corner branch, offset is clamped to a small minimum before the ellipse computation
  // to avoid division-by-zero in inversesqrt().

  fragBuilder->codeAppendf("vec2 offset = %s.xy;", ellipseOffsets.fsIn().c_str());
  fragBuilder->codeAppendf("vec2 eDist = %s;", edgeDist.fsIn().c_str());

  fragBuilder->codeAppend("bool hasXCurve = offset.x > 0.0;");
  fragBuilder->codeAppend("bool hasYCurve = offset.y > 0.0;");

  fragBuilder->codeAppend("float outerAlpha;");
  fragBuilder->codeAppend("if (hasXCurve && hasYCurve) {");
  // Corner region: clamp offset to avoid zero in gradient computation
  fragBuilder->codeAppend("  vec2 safeOffset = max(offset, vec2(1.0/4096.0));");
  if (stroke) {
    fragBuilder->codeAppendf("  safeOffset *= %s.xy;", ellipseRadii.fsIn().c_str());
  }
  fragBuilder->codeAppend("  float test = dot(safeOffset, safeOffset) - 1.0;");
  fragBuilder->codeAppendf("  vec2 grad = 2.0*safeOffset*%s.xy;", ellipseRadii.fsIn().c_str());
  fragBuilder->codeAppend("  float gradDot = max(dot(grad, grad), 1.1755e-38);");
  fragBuilder->codeAppend("  float invlen = inversesqrt(gradDot);");
  fragBuilder->codeAppend("  outerAlpha = clamp(0.5-test*invlen, 0.0, 1.0);");
  fragBuilder->codeAppend("} else if (hasYCurve) {");
  // Horizontal edge region (top/bottom): use vertical edge distance
  fragBuilder->codeAppend("  outerAlpha = clamp(0.5 + eDist.y, 0.0, 1.0);");
  fragBuilder->codeAppend("} else if (hasXCurve) {");
  // Vertical edge region (left/right): use horizontal edge distance
  fragBuilder->codeAppend("  outerAlpha = clamp(0.5 + eDist.x, 0.0, 1.0);");
  fragBuilder->codeAppend("} else {");
  // Center region: full coverage
  fragBuilder->codeAppend("  outerAlpha = 1.0;");
  fragBuilder->codeAppend("}");

  // Inner ellipse (stroke mode)
  if (stroke) {
    fragBuilder->codeAppendf("vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
    fragBuilder->codeAppend("float innerAlpha;");
    fragBuilder->codeAppend("if (hasXCurve && hasYCurve) {");
    // Corner region: inner ellipse Sampson distance
    fragBuilder->codeAppendf("  vec2 innerOffset = max(offset, vec2(1.0/4096.0)) * %s.zw;",
                             ellipseRadii.fsIn().c_str());
    fragBuilder->codeAppend("  float innerTest = dot(innerOffset, innerOffset) - 1.0;");
    fragBuilder->codeAppendf("  vec2 innerGrad = 2.0*innerOffset*%s.zw;",
                             ellipseRadii.fsIn().c_str());
    fragBuilder->codeAppend("  float innerGradDot = max(dot(innerGrad, innerGrad), 1.1755e-38);");
    fragBuilder->codeAppend("  float innerInvlen = inversesqrt(innerGradDot);");
    fragBuilder->codeAppend("  innerAlpha = clamp(0.5+innerTest*innerInvlen, 0.0, 1.0);");
    fragBuilder->codeAppend("} else if (hasYCurve) {");
    // Horizontal edge: inner coverage from edgeDist.y minus full stroke width
    fragBuilder->codeAppend("  innerAlpha = clamp(0.5 - (eDist.y - 2.0*sw.y), 0.0, 1.0);");
    fragBuilder->codeAppend("} else if (hasXCurve) {");
    // Vertical edge: inner coverage from edgeDist.x minus full stroke width
    fragBuilder->codeAppend("  innerAlpha = clamp(0.5 - (eDist.x - 2.0*sw.x), 0.0, 1.0);");
    fragBuilder->codeAppend("} else {");
    // Center region: use min of both inner edge distances
    fragBuilder->codeAppend("  float innerEdgeX = eDist.x - 2.0*sw.x;");
    fragBuilder->codeAppend("  float innerEdgeY = eDist.y - 2.0*sw.y;");
    fragBuilder->codeAppend("  innerAlpha = clamp(0.5 - min(innerEdgeX, innerEdgeY), 0.0, 1.0);");
    fragBuilder->codeAppend("}");
    fragBuilder->codeAppend("outerAlpha *= innerAlpha;");
  }

  fragBuilder->codeAppendf("%s = vec4(outerAlpha);", args.outputCoverage.c_str());
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
