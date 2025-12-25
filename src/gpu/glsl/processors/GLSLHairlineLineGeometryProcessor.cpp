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

#include "GLSLHairlineLineGeometryProcessor.h"

namespace tgfx {

PlacementPtr<HairlineLineGeometryProcessor> HairlineLineGeometryProcessor::Make(
    BlockAllocator* allocator, const PMColor& color, const Matrix& viewMatrix,
    std::optional<Matrix> uvMatrix, uint8_t coverage) {
  return allocator->make<GLSLHairlineLineGeometryProcessor>(color, viewMatrix, uvMatrix, coverage);
}

GLSLHairlineLineGeometryProcessor::GLSLHairlineLineGeometryProcessor(const PMColor& color,
                                                                     const Matrix& viewMatrix,
                                                                     std::optional<Matrix> uvMatrix,
                                                                     uint8_t coverage)
    : HairlineLineGeometryProcessor(color, viewMatrix, uvMatrix, coverage) {
}

void GLSLHairlineLineGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit vertex attributes
  varyingHandler->emitAttributes(*this);

  auto matrixName =
      uniformHandler->addUniform("Matrix", UniformFormat::Float3x3, ShaderStage::Vertex);
  std::string positionName = "position";
  vertBuilder->codeAppendf("vec2 %s = (%s * vec3(%s, 1.0)).xy;", positionName.c_str(),
                           matrixName.c_str(), position.name().c_str());

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(position));
  // Pass edge distance to fragment shader for anti-aliasing
  auto edgeVarying = varyingHandler->addVarying("EdgeDistance", SLType::Float);
  vertBuilder->codeAppendf("%s = %s;", edgeVarying.vsOut().c_str(), edgeDistance.name().c_str());

  // Fragment shader: calculate anti-aliasing based on edge distance
  fragBuilder->codeAppendf("float edgeAlpha = abs(%s);", edgeVarying.fsIn().c_str());
  fragBuilder->codeAppend("edgeAlpha = clamp(edgeAlpha, 0.0, 1.0);");
  // if AAtype is none
  // fragBuilder->codeAppend("edgeAlpha = edgeAlpha > 0.5 ? 1.0 : 0.0;");

  auto colorName =
      uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());

  if (coverage != 0xFF) {
    auto coverageScale =
        uniformHandler->addUniform("Coverage", UniformFormat::Float, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = vec4(%s * edgeAlpha);", args.outputCoverage.c_str(),
                             coverageScale.c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.outputCoverage.c_str());
  }
  // Emit vertex position
  vertBuilder->emitNormalizedPosition(positionName);
}

void GLSLHairlineLineGeometryProcessor::setData(UniformData* vertexUniformData,
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
