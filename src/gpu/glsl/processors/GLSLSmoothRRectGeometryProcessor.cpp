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

#include "GLSLSmoothRRectGeometryProcessor.h"
#include "core/SmoothCornerCoeffs.h"
#include "gpu/glsl/GLSLSmoothCorner.h"

namespace tgfx {
PlacementPtr<SmoothRRectGeometryProcessor> SmoothRRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, float smoothness, bool antiAlias,
    std::optional<PMColor> commonColor, bool elliptical, bool stroke) {
  return allocator->make<GLSLSmoothRRectGeometryProcessor>(width, height, smoothness, antiAlias,
                                                           commonColor, elliptical, stroke);
}

GLSLSmoothRRectGeometryProcessor::GLSLSmoothRRectGeometryProcessor(
    int width, int height, float smoothness, bool antiAlias, std::optional<PMColor> commonColor,
    bool elliptical, bool stroke)
    : SmoothRRectGeometryProcessor(width, height, smoothness, antiAlias, commonColor, elliptical,
                                   stroke) {
}

void GLSLSmoothRRectGeometryProcessor::emitCode(EmitArgs& args) const {
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
  auto radiiVarying = varyingHandler->addVarying("radii", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", radiiVarying.vsOut().c_str(), inRadii.name().c_str());

  // Elliptical variant: pass per-corner y radii alongside.
  Varying radiiYVarying;
  if (elliptical) {
    radiiYVarying = varyingHandler->addVarying("radiiY", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", radiiYVarying.vsOut().c_str(), inRadiiY.name().c_str());
  }

  // Pass rect bounds to fragment shader
  auto boundsVarying = varyingHandler->addVarying("rectBounds", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", boundsVarying.vsOut().c_str(), inRectBounds.name().c_str());

  // Pass half stroke width to fragment shader (stroke variant only)
  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("strokeWidth", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", strokeWidthVarying.vsOut().c_str(),
                             inStrokeWidth.name().c_str());
  }

  // Emit transforms using position as UV coordinates.
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(inPosition.name(), SLType::Float2));

  const auto coeffsName =
      uniformHandler->addUniform("SmoothCoeffs", UniformFormat::Float4, ShaderStage::Fragment);
  const auto arcCosSqName =
      uniformHandler->addUniform("SmoothArcCosSq", UniformFormat::Float, ShaderStage::Fragment);
  const auto antiAliasName =
      uniformHandler->addUniform("AntiAlias", UniformFormat::Float, ShaderStage::Fragment);
  std::string ellipticalDistName;
  const auto smoothCornerDistName =
      EmitSmoothCornerFunctions(fragBuilder, elliptical ? &ellipticalDistName : nullptr);

  // Fragment shader - evaluate the smoothed round rect shape.
  fragBuilder->codeAppendf("highp vec2 localCoord = %s;", localCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("highp vec4 radii = %s;", radiiVarying.fsIn().c_str());
  if (elliptical) {
    fragBuilder->codeAppendf("highp vec4 radiiY = %s;", radiiYVarying.fsIn().c_str());
  }
  fragBuilder->codeAppendf("highp vec4 bounds = %s;", boundsVarying.fsIn().c_str());

  // Corner-box dispatch. Each corner owns a quadrant-local box widened to the smoothed corner
  // footprint. Order matches the vertex data: TL, TR, BR, BL.
  fragBuilder->codeAppend("highp vec4 cornersX = vec4(bounds.x, bounds.z, bounds.z, bounds.x);");
  fragBuilder->codeAppend("highp vec4 cornersY = vec4(bounds.y, bounds.y, bounds.w, bounds.w);");
  // signsX/Y flip the local coordinate so that all four corner-box queries become first-quadrant.
  fragBuilder->codeAppend("vec4 signsX = vec4(1.0, -1.0, -1.0, 1.0);");
  fragBuilder->codeAppend("vec4 signsY = vec4(1.0, 1.0, -1.0, -1.0);");
  fragBuilder->codeAppend("highp vec4 dx = (vec4(localCoord.x) - cornersX) * signsX;");
  fragBuilder->codeAppend("highp vec4 dy = (vec4(localCoord.y) - cornersY) * signsY;");
  // inBox is 1.0 for each corner whose widened box contains the fragment. Edge competition in
  // RRect keeps the widened boxes of adjacent corners disjoint. The elliptical variant widens
  // each axis by its own radius.
  if (elliptical) {
    fragBuilder->codeAppendf("vec4 inBox = step(dx, radii * %s.x) * step(dy, radiiY * %s.x);",
                             coeffsName.c_str(), coeffsName.c_str());
  } else {
    fragBuilder->codeAppendf("vec4 inBox = step(dx, radii * %s.x) * step(dy, radii * %s.x);",
                             coeffsName.c_str(), coeffsName.c_str());
  }
  // One-hot collapse: when adjacent corner boxes share a fragment along their boundary, keep
  // the first hit so subsequent queries collapse to a single corner.
  fragBuilder->codeAppend(
      "vec4 prefixSum = vec4(0.0, inBox.x, inBox.x + inBox.y, "
      "inBox.x + inBox.y + inBox.z);");
  fragBuilder->codeAppend("inBox *= step(prefixSum, vec4(0.5));");
  fragBuilder->codeAppend("float inAnyCorner = dot(inBox, vec4(1.0));");

  fragBuilder->codeAppend("highp float r = dot(inBox, radii);");
  if (elliptical) {
    fragBuilder->codeAppend("highp float ry = dot(inBox, radiiY);");
  }
  // Map the fragment into the selected corner's frame: vertex at the origin, edges along the
  // +x/+y axes, interior in the first quadrant. The signs double up as the mirroring factors.
  fragBuilder->codeAppend(
      "highp vec2 cornerVertex = vec2(dot(inBox, cornersX), dot(inBox, cornersY));");
  fragBuilder->codeAppend("highp vec2 cornerSigns = vec2(dot(inBox, signsX), dot(inBox, signsY));");
  fragBuilder->codeAppend("highp vec2 q = (localCoord - cornerVertex) * cornerSigns;");
  fragBuilder->codeAppend("highp float cornerDist = 0.0;");
  // Zero-radius corners keep a clamped circular evaluation: their box only catches the outside
  // quadrant past the vertex, where a sub-pixel radius gives the sharp-corner result.
  fragBuilder->codeAppend("if (r > 0.0) {");
  if (elliptical) {
    fragBuilder->codeAppendf("cornerDist = %s(q, vec2(r, ry), %s, %s);", ellipticalDistName.c_str(),
                             coeffsName.c_str(), arcCosSqName.c_str());
  } else {
    fragBuilder->codeAppendf("cornerDist = %s(q, r, %s, %s);", smoothCornerDistName.c_str(),
                             coeffsName.c_str(), arcCosSqName.c_str());
  }
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend("highp vec2 offset = (localCoord - cornerVertex) / 0.5;");
  fragBuilder->codeAppend("highp vec2 safeOffset = max(abs(offset), vec2(1.0/4096.0));");
  fragBuilder->codeAppend("highp float test = dot(safeOffset, safeOffset) - 1.0;");
  fragBuilder->codeAppend("highp vec2 grad = 2.0 * safeOffset / 0.5;");
  fragBuilder->codeAppend("highp float gradDot = max(dot(grad, grad), 1.1755e-38);");
  fragBuilder->codeAppend("cornerDist = test * inversesqrt(gradDot);");
  fragBuilder->codeAppend("}");

  // Edge / center distance (negative inside, positive outside).
  fragBuilder->codeAppend("highp vec2 d0 = localCoord - bounds.xy;");
  fragBuilder->codeAppend("highp vec2 d1 = bounds.zw - localCoord;");
  fragBuilder->codeAppend("highp float edgeDist = -min(min(d0.x, d0.y), min(d1.x, d1.y));");

  fragBuilder->codeAppend("highp float dist = mix(edgeDist, cornerDist, inAnyCorner);");

  if (stroke) {
    // Stroke: the same signed distance field is reused, and the coverage is computed from the
    // ring distance |dist| - halfStroke, so the ring's outer edge follows the true offset curve
    // of the contour. Sharp corners cannot reach this path (blocked by UseDrawPath upstream).
    fragBuilder->codeAppendf("highp float sw = %s;", strokeWidthVarying.fsIn().c_str());
    fragBuilder->codeAppend("dist = abs(dist) - sw;");
  }
  // AA / NonAA coverage via uniform mix.
  fragBuilder->codeAppend("highp float aaCov = clamp(0.5 - dist, 0.0, 1.0);");
  fragBuilder->codeAppend("highp float nonAACov = step(dist, 0.0);");
  fragBuilder->codeAppendf("highp float coverage = mix(nonAACov, aaCov, %s);",
                           antiAliasName.c_str());
  fragBuilder->codeAppendf("%s = vec4(coverage);", args.outputCoverage.c_str());
}

void GLSLSmoothRRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                               UniformData* fragmentUniformData,
                                               FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  const auto coeffs = ComputeSmoothCornerCoeffs(smoothness);
  const std::array<float, 4> coeffsValues = {coeffs.tangentRatio, coeffs.controlStep,
                                             coeffs.endOffsetX, coeffs.endOffsetY};
  fragmentUniformData->setData("SmoothCoeffs", coeffsValues);
  fragmentUniformData->setData("SmoothArcCosSq", coeffs.arcCosSquared);
  const float antiAliasValue = antiAlias ? 1.0f : 0.0f;
  fragmentUniformData->setData("AntiAlias", antiAliasValue);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
