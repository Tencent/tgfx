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
                                                                     float coverage,
                                                                     AAType aaType)
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

  // Fragment shader: Loop-Blinn quadratic curve anti-aliasing
  // The UV coordinates are set up so that the curve is defined by the implicit equation: u^2 - v = 0
  // Points where u^2 < v are inside the curve, u^2 > v are outside.
  const char* edge = edgeVarying.fsIn().c_str();
  fragBuilder->codeAppendf("float edgeAlpha;");
  // Compute screen-space partial derivatives of UV coordinates for gradient calculation
  fragBuilder->codeAppendf("vec2 duvdx = vec2(dFdx(%s.xy));", edge);
  fragBuilder->codeAppendf("vec2 duvdy = vec2(dFdy(%s.xy));", edge);
  // Compute gradient of the implicit function F(u,v) = u^2 - v
  // gradF = (dF/dx, dF/dy) where dF/dx = 2u * du/dx - dv/dx, dF/dy = 2u * du/dy - dv/dy
  fragBuilder->codeAppendf(
      "vec2 gF = vec2(2.0 * %s.x * duvdx.x - duvdx.y,"
      "               2.0 * %s.x * duvdy.x - duvdy.y);",
      edge, edge);
  // Evaluate the implicit function: F = u^2 - v (positive outside, negative inside)
  fragBuilder->codeAppendf("edgeAlpha = float(%s.x * %s.x - %s.y);", edge, edge, edge);
  // Convert to signed distance and normalize by gradient magnitude for anti-aliasing
  fragBuilder->codeAppend("edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));");
  // Invert so that 1.0 = on curve, 0.0 = far from curve
  fragBuilder->codeAppend("edgeAlpha = max(1.0 - edgeAlpha, 0.0);");
  if (aaType != AAType::Coverage) {
    // Non-coverage AA: threshold to binary
    fragBuilder->codeAppend("edgeAlpha = edgeAlpha >= 0.5 ? 1.0 : 0.0;");
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
