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

#include "GLSLFillRRectGeometryProcessor.h"

namespace tgfx {
PlacementPtr<FillRRectGeometryProcessor> FillRRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, AAType aaType,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLFillRRectGeometryProcessor>(width, height, aaType, commonColor);
}

GLSLFillRRectGeometryProcessor::GLSLFillRRectGeometryProcessor(int width, int height, AAType aaType,
                                                               std::optional<PMColor> commonColor)
    : FillRRectGeometryProcessor(width, height, aaType, commonColor) {
}

void GLSLFillRRectGeometryProcessor::emitCode(EmitArgs& args) const {
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

  // AA bloat multiplier based on AAType
  int aaBloatMultiplier = 1;  // Default: outset one half pixel (1 radius)
  if (aaType == AAType::MSAA) {
    aaBloatMultiplier = 2;  // Outset an entire pixel (2 radii)
  } else if (aaType == AAType::None) {
    aaBloatMultiplier = 0;  // No AA bloat
  }
  vertBuilder->codeAppendf("float aa_bloat_multiplier = %d.0;", aaBloatMultiplier);

  // Unpack vertex attributes
  vertBuilder->codeAppendf("vec2 corner = %s.xy;", inCornerAndRadius.name().c_str());
  vertBuilder->codeAppendf("vec2 radius_outset = %s.zw;", inCornerAndRadius.name().c_str());
  vertBuilder->codeAppendf("vec2 aa_bloat_direction = %s.xy;", inAABloatCoverage.name().c_str());
  vertBuilder->codeAppendf("float is_linear_coverage = %s.w;", inAABloatCoverage.name().c_str());

  // Get skew matrix components
  vertBuilder->codeAppendf("vec4 skew = %s;", inSkew.name().c_str());

  // Find the amount to bloat each edge for AA (in source space)
  vertBuilder->codeAppend("vec2 pixellength = inversesqrt(vec2(dot(skew.xz, skew.xz), "
                          "dot(skew.yw, skew.yw)));");
  vertBuilder->codeAppend("vec4 normalized_axis_dirs = skew * vec4(pixellength.x, pixellength.x, "
                          "pixellength.y, pixellength.y);");
  vertBuilder->codeAppend("vec2 axiswidths = abs(normalized_axis_dirs.xy) + "
                          "abs(normalized_axis_dirs.zw);");
  vertBuilder->codeAppend("vec2 aa_bloatradius = axiswidths * pixellength * 0.5;");

  // For simple RRect, all corners have the same radii.
  vertBuilder->codeAppendf("vec2 radii = %s;", inRadii.name().c_str());

  vertBuilder->codeAppend("float coverage_multiplier = 1.0;");
  vertBuilder->codeAppend("if (any(greaterThan(aa_bloatradius, vec2(1.0)))) {");
  // The rrect is more narrow than a half-pixel AA coverage ramp. Fudge the size up
  // to the width of a coverage ramp, and then reduce total coverage.
  vertBuilder->codeAppend("  corner = max(abs(corner), aa_bloatradius) * sign(corner);");
  vertBuilder->codeAppend("  coverage_multiplier = 1.0 / (max(aa_bloatradius.x, 1.0) * "
                          "max(aa_bloatradius.y, 1.0));");
  // Set radii to zero to ensure we take the "linear coverage" codepath.
  vertBuilder->codeAppend("  radii = vec2(0.0);");
  vertBuilder->codeAppend("}");

  // Unpack coverage
  vertBuilder->codeAppendf("float coverage = %s.z;", inAABloatCoverage.name().c_str());

  vertBuilder->codeAppend("if (any(lessThan(radii, aa_bloatradius * 1.5))) {");
  // The radii are very small. Demote this arc to a sharp 90 degree corner.
  vertBuilder->codeAppend("  radii = vec2(0.0);");
  // Convert to a standard picture frame for an AA rect instead of the round rect geometry.
  vertBuilder->codeAppend("  aa_bloat_direction = sign(corner);");
  vertBuilder->codeAppend("  if (coverage > 0.5) {");  // Are we an inset edge?
  vertBuilder->codeAppend("    aa_bloat_direction = -aa_bloat_direction;");
  vertBuilder->codeAppend("  }");
  vertBuilder->codeAppend("  is_linear_coverage = 1.0;");
  vertBuilder->codeAppend("} else {");
  // Don't let radii get smaller than a coverage ramp plus an extra half pixel for MSAA.
  vertBuilder->codeAppend("  radii = clamp(radii, pixellength * 1.5, 2.0 - pixellength * 1.5);");
  // For simple RRect, neighbor_radii == radii, so spacing = 2.0 - 2*radii.
  // Don't let neighboring radii get closer together than 1/16 pixel.
  vertBuilder->codeAppend("  vec2 spacing = 2.0 - radii * 2.0;");
  vertBuilder->codeAppend("  vec2 extra_pad = max(pixellength * 0.0625 - spacing, vec2(0.0));");
  vertBuilder->codeAppend("  radii -= extra_pad * 0.5;");
  vertBuilder->codeAppend("}");

  // Find our vertex position, adjusted for radii and bloated for AA.
  vertBuilder->codeAppend(
      "vec2 aa_outset = aa_bloat_direction * aa_bloatradius * aa_bloat_multiplier;");
  vertBuilder->codeAppend("vec2 vertexpos = corner + radius_outset * radii + aa_outset;");

  vertBuilder->codeAppend("if (coverage > 0.5) {");  // Are we an inset edge?
  // Don't allow the aa insets to overlap. i.e., Don't let them inset past the center (x=y=0).
  vertBuilder->codeAppend("  if (aa_bloat_direction.x != 0.0 && vertexpos.x * corner.x < 0.0) {");
  vertBuilder->codeAppend("    float backset = abs(vertexpos.x);");
  vertBuilder->codeAppend("    vertexpos.x = 0.0;");
  vertBuilder->codeAppend(
      "    vertexpos.y += backset * sign(corner.y) * pixellength.y / pixellength.x;");
  vertBuilder->codeAppend("    coverage = (coverage - 0.5) * abs(corner.x) / "
                          "(abs(corner.x) + backset) + 0.5;");
  vertBuilder->codeAppend("  }");
  vertBuilder->codeAppend("  if (aa_bloat_direction.y != 0.0 && vertexpos.y * corner.y < 0.0) {");
  vertBuilder->codeAppend("    float backset = abs(vertexpos.y);");
  vertBuilder->codeAppend("    vertexpos.y = 0.0;");
  vertBuilder->codeAppend(
      "    vertexpos.x += backset * sign(corner.x) * pixellength.x / pixellength.y;");
  vertBuilder->codeAppend("    coverage = (coverage - 0.5) * abs(corner.y) / "
                          "(abs(corner.y) + backset) + 0.5;");
  vertBuilder->codeAppend("  }");
  vertBuilder->codeAppend("}");

  // Transform to device space
  // The skew matrix is stored as [scaleX, skewX, skewY, scaleY]
  // For row-vector * matrix multiplication (vertexpos * skewmatrix):
  // GLSL mat2 is column-major, so mat2(col0, col1) creates:
  //   | col0.x  col1.x |
  //   | col0.y  col1.y |
  // mat2(skew.xy, skew.zw) creates:
  //   | scaleX  skewY |
  //   | skewX   scaleY |
  // Then vertexpos * skewmatrix = (vx, vy) * mat2 gives:
  //   (vx*scaleX + vy*skewX, vx*skewY + vy*scaleY)
  // which is the correct transformation.
  vertBuilder->codeAppend("mat2 skewmatrix = mat2(skew.xy, skew.zw);");
  vertBuilder->codeAppendf("vec2 devcoord = vertexpos * skewmatrix + %s;",
                           inTranslate.name().c_str());

  // Output position using RTAdjust uniform
  vertBuilder->emitNormalizedPosition("devcoord");

  // Setup varyings for fragment shader
  auto arcCoordVarying = varyingHandler->addVarying("arcCoord", SLType::Float4);
  vertBuilder->codeAppend("if (is_linear_coverage != 0.0) {");
  // We are a non-corner piece: Set x=0 to indicate built-in coverage, and interpolate
  // linear coverage across y.
  vertBuilder->codeAppendf("  %s = vec4(0.0, coverage * coverage_multiplier, 0.0, 0.0);",
                           arcCoordVarying.vsOut().c_str());
  vertBuilder->codeAppend("} else {");
  // Find the normalized arc coordinates for our corner ellipse.
  vertBuilder->codeAppend("  vec2 arccoord = 1.0 - abs(radius_outset) + aa_outset / radii * corner;");
  // We are a corner piece: Interpolate the arc coordinates for coverage.
  // Emit x+1 to ensure no pixel in the arc has a x value of 0.
  vertBuilder->codeAppend("  mat2 derivatives = inverse(skewmatrix);");
  vertBuilder->codeAppendf("  %s = vec4(arccoord.x + 1.0, arccoord.y, "
                           "derivatives[0] * arccoord / radii * 2.0);",
                           arcCoordVarying.vsOut().c_str());
  vertBuilder->codeAppend("}");

  // Emit transforms using device coordinates as UV coordinates.
  // This matches the behavior of EllipseGeometryProcessor and other processors.
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar("devcoord", SLType::Float2));

  // Fragment shader
  fragBuilder->codeAppendf("float x_plus_1 = %s.x;", arcCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("float y = %s.y;", arcCoordVarying.fsIn().c_str());
  fragBuilder->codeAppend("float coverage;");
  fragBuilder->codeAppend("if (x_plus_1 == 0.0) {");
  fragBuilder->codeAppend("  coverage = y;");  // We are a non-arc pixel (linear coverage)
  fragBuilder->codeAppend("} else {");
  fragBuilder->codeAppend("  float fn = x_plus_1 * (x_plus_1 - 2.0);");  // fn = (x+1)*(x-1) = x^2-1
  fragBuilder->codeAppend("  fn = y * y + fn;");                          // fn = x^2 + y^2 - 1
  // The gradient is interpolated across arccoord.zw
  fragBuilder->codeAppendf("  float gx = %s.z;", arcCoordVarying.fsIn().c_str());
  fragBuilder->codeAppendf("  float gy = %s.w;", arcCoordVarying.fsIn().c_str());
  fragBuilder->codeAppend("  float fnwidth = abs(gx) + abs(gy);");
  fragBuilder->codeAppend("  coverage = 0.5 - fn / fnwidth;");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("coverage = clamp(coverage, 0.0, 1.0);");

  // For non-AA, quantize coverage to 0 or 1
  if (aaType == AAType::None) {
    fragBuilder->codeAppend("coverage = (coverage >= 0.5) ? 1.0 : 0.0;");
  }

  fragBuilder->codeAppendf("%s = vec4(coverage);", args.outputCoverage.c_str());
}

void GLSLFillRRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                             UniformData* fragmentUniformData,
                                             FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
