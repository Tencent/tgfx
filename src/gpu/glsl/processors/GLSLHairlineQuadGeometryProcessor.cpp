/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLSLHairlineQuadGeometryProcessor.h"
#include "tgfx/core/Color.h"

namespace tgfx {

PlacementPtr<HairlineQuadGeometryProcessor> HairlineQuadGeometryProcessor::Make(
    BlockAllocator* allocator, const PMColor& color, const Matrix& viewMatrix,
    std::optional<Matrix> uvMatrix, float coverage, AAType aaType) {
  return allocator->make<GLSLHairlineQuadGeometryProcessor>(color, viewMatrix, uvMatrix, coverage,
                                                            aaType);
}

GLSLHairlineQuadGeometryProcessor::GLSLHairlineQuadGeometryProcessor(const PMColor& color,
                                                                     const Matrix& viewMatrix,
                                                                     std::optional<Matrix> uvMatrix,
                                                                     float coverage, AAType aaType)
    : HairlineQuadGeometryProcessor(color, viewMatrix, uvMatrix, coverage, aaType) {
}

void GLSLHairlineQuadGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit vertex attributes
  varyingHandler->emitAttributes(*this);

  // Transform vertex position by view matrix
  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "transformedPosition";
  vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(positionName, SLType::Float2));

  // Pass quad edge UV coordinates to fragment shader for curve evaluation
  auto edgeVarying = varyingHandler->addVarying("HairQuadEdge", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", edgeVarying.vsOut().c_str(), hairQuadEdge.name().c_str());

  // Fragment shader: Loop-Blinn quadratic curve evaluation
  // The UV coordinates are set up so that the curve is defined by the implicit equation: u^2 - v = 0
  // Points where u^2 < v are inside the curve, u^2 > v are outside.
  const char* edge = edgeVarying.fsIn().c_str();
  fragBuilder->codeAppendf("float edgeAlpha;");

  if (aaType == AAType::Coverage) {
    // Coverage AA: Use gradient-based soft edge for anti-aliasing
    // Compute screen-space derivatives for proper AA width calculation
    fragBuilder->codeAppendf("vec2 duvdx = vec2(dFdx(%s.xy));", edge);
    fragBuilder->codeAppendf("vec2 duvdy = vec2(dFdy(%s.xy));", edge);
    // Gradient of the implicit function F = u^2 - v: dF/du = 2u, dF/dv = -1
    fragBuilder->codeAppendf(
        "vec2 gF = vec2(2.0 * %s.x * duvdx.x - duvdx.y, 2.0 * %s.x * duvdy.x - duvdy.y);", edge,
        edge);
    // Evaluate the implicit function: F = u^2 - v (positive outside, negative inside)
    fragBuilder->codeAppendf("edgeAlpha = float(%s.x * %s.x - %s.y);", edge, edge, edge);
    // Distance to edge in screen space, normalized by gradient magnitude
    fragBuilder->codeAppendf("edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));");
    // Convert to coverage: 1.0 at center, 0.0 at edge, with smooth falloff
    fragBuilder->codeAppendf("edgeAlpha = max(1.0 - edgeAlpha, 0.0);");
  } else {
    // No AA (for SSAA mode or explicit non-AA): Use hard edge
    // When SSAA is enabled, the supersampling provides AA, so per-pixel AA is redundant
    fragBuilder->codeAppendf("edgeAlpha = (%s.x * %s.x - %s.y) <= 0.0 ? 1.0 : 0.0;", edge, edge,
                             edge);
  }

  // Output color and coverage
  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  auto coverageScale =
      uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = vec4(%s * edgeAlpha);", args.outputCoverage.c_str(),
                           coverageScale.c_str());

  // Emit final vertex position
  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLHairlineQuadGeometryProcessor::setData(UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData,
                                                FPCoordTransformIter* transformIter) const {
  if (uvMatrix.has_value()) {
    setTransformDataHelper(*uvMatrix, vertexUniformData, transformIter);
  }
  fragmentUniformData->setData("Color", color);
  vertexUniformData->setData("Matrix", viewMatrix);
  fragmentUniformData->setData("Coverage", coverage);
}

}  // namespace tgfx
