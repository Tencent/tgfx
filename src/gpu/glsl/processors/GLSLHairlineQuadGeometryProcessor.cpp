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

// Enable this macro to use solid color fill for testing triangle rendering.
// #define HAIRLINE_QUAD_DEBUG_SOLID_FILL

namespace tgfx {

PlacementPtr<HairlineQuadGeometryProcessor> HairlineQuadGeometryProcessor::Make(
    BlockAllocator* allocator, const PMColor& color, const Matrix& viewMatrix,
    std::optional<Matrix> uvMatrix, uint8_t coverage) {
  return allocator->make<GLSLHairlineQuadGeometryProcessor>(color, viewMatrix, uvMatrix, coverage);
}

GLSLHairlineQuadGeometryProcessor::GLSLHairlineQuadGeometryProcessor(const PMColor& color,
                                                                     const Matrix& viewMatrix,
                                                                     std::optional<Matrix> uvMatrix,
                                                                     uint8_t coverage)
    : HairlineQuadGeometryProcessor(color, viewMatrix, uvMatrix, coverage) {
}

void GLSLHairlineQuadGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit vertex attributes
  varyingHandler->emitAttributes(*this);

  auto matrixName =
      args.uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));

#ifdef HAIRLINE_QUAD_DEBUG_SOLID_FILL
  // Debug mode: Solid color fill without bezier curve calculation
  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());

  if (coverage != 0xFF) {
    auto coverageScale =
        uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(), coverageScale.c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }
#else
  // Production mode: Calculate edge alpha based on quadratic bezier distance field
  auto edgeVarying = varyingHandler->addVarying("HairQuadEdge", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", edgeVarying.vsOut().c_str(), hairQuadEdge.name().c_str());

  fragBuilder->codeAppendf("float edgeAlpha;");
  fragBuilder->codeAppendf("vec2 duvdx = vec2(dFdx(%s.xy));", edgeVarying.fsIn().c_str());
  fragBuilder->codeAppendf("vec2 duvdy = vec2(dFdy(%s.xy));", edgeVarying.fsIn().c_str());
  fragBuilder->codeAppendf(
      "vec2 gF = vec2(2.0 * %s.x * duvdx.x - duvdx.y,"
      "               2.0 * %s.x * duvdy.x - duvdy.y);",
      edgeVarying.fsIn().c_str(), edgeVarying.fsIn().c_str());
  fragBuilder->codeAppendf("edgeAlpha = float(%s.x * %s.x - %s.y);", edgeVarying.fsIn().c_str(),
                           edgeVarying.fsIn().c_str(), edgeVarying.fsIn().c_str());
  fragBuilder->codeAppend("edgeAlpha = sqrt(edgeAlpha * edgeAlpha / dot(gF, gF));");
  fragBuilder->codeAppend("edgeAlpha = max(1.0 - edgeAlpha, 0.0);");
  // if AAtype is none
  // fragBuilder->codeAppend("edgeAlpha = edgeAlpha > 0.5 ? 1.0 : 0.0;");

  auto colorName =
      args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());

  if (coverage != 0xFF) {
    auto coverageScale =
        uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = vec4(%s * edgeAlpha);", args.outputCoverage.c_str(),
                             coverageScale.c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.outputCoverage.c_str());
  }
#endif

  vertBuilder->emitNormalizedPosition(position.name());
}

void GLSLHairlineQuadGeometryProcessor::setData(UniformData* vertexUniformData,
                                                UniformData* fragmentUniformData,
                                                FPCoordTransformIter* transformIter) const {
  if (uvMatrix.has_value()) {
    setTransformDataHelper(*uvMatrix, vertexUniformData, transformIter);
  }
  fragmentUniformData->setData("Color", color);
  vertexUniformData->setData("Matrix", viewMatrix);
  if (coverage != 0xFF) {
    fragmentUniformData->setData("Coverage", static_cast<float>(coverage) / 255.0f);
  }
}

}  // namespace tgfx
