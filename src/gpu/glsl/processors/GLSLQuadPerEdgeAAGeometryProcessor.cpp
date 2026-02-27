/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "GLSLQuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {
PlacementPtr<QuadPerEdgeAAGeometryProcessor> QuadPerEdgeAAGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, AAType aa, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset) {
  return allocator->make<GLSLQuadPerEdgeAAGeometryProcessor>(width, height, aa, commonColor,
                                                             uvMatrix, hasSubset);
}

GLSLQuadPerEdgeAAGeometryProcessor::GLSLQuadPerEdgeAAGeometryProcessor(
    int width, int height, AAType aa, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset)
    : QuadPerEdgeAAGeometryProcessor(width, height, aa, commonColor, uvMatrix, hasSubset) {
}

void GLSLQuadPerEdgeAAGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto& uvCoordsVar = uvCoord.empty() ? position : uvCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  if (aa == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), coverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), color.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(position.name());
}

void GLSLQuadPerEdgeAAGeometryProcessor::setData(UniformData* vertexUniformData,
                                                 UniformData* fragmentUniformData,
                                                 FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}

void GLSLQuadPerEdgeAAGeometryProcessor::onSetTransformData(UniformData* uniformData,
                                                            const CoordTransform* coordTransform,
                                                            int index) const {
  if (index == 0 && !subset.empty() && uvCoord.empty()) {
    // Subset only applies to the first image in ProgramInfo.
    uniformData->setData("texSubsetMatrix", coordTransform->getTotalMatrix());
  }
}

void GLSLQuadPerEdgeAAGeometryProcessor::onEmitTransform(EmitArgs& args,
                                                         VertexShaderBuilder* vertexBuilder,
                                                         VaryingHandler* varyingHandler,
                                                         UniformHandler* uniformHandler,
                                                         const std::string& transformUniformName,
                                                         bool hasPerspective, int index) const {
  if (index == 0 && !subset.empty()) {
    auto varying = varyingHandler->addVarying("vTexSubset", SLType::Float4, true);
    std::string subsetMatrixName = transformUniformName;
    if (uvCoord.empty()) {
      subsetMatrixName = uniformHandler->addUniform("texSubsetMatrix", UniformFormat::Float3x3,
                                                    ShaderStage::Vertex);
    }
    const std::string srcLT = "srcLT";
    const std::string srcRB = "srcRB";
    const std::string perspLT = "perspLT";
    const std::string perspRB = "perspRB";
    vertexBuilder->codeAppendf("highp vec2 %s = %s.xy;", srcLT.c_str(), subset.name().c_str());
    vertexBuilder->codeAppendf("highp vec2 %s = %s.zw;", srcRB.c_str(), subset.name().c_str());
    vertexBuilder->emitTransformedPoint(perspLT, srcLT, subsetMatrixName, hasPerspective);
    vertexBuilder->emitTransformedPoint(perspRB, srcRB, subsetMatrixName, hasPerspective);
    vertexBuilder->codeAppend("highp vec4 subset = vec4(" + perspLT + ", " + perspRB + ");");
    vertexBuilder->codeAppend("if (subset.x > subset.z) {");
    vertexBuilder->codeAppend("  highp float tmp = subset.x;");
    vertexBuilder->codeAppend("  subset.x = subset.z;");
    vertexBuilder->codeAppend("  subset.z = tmp;");
    vertexBuilder->codeAppend("}");
    vertexBuilder->codeAppend("if (subset.y > subset.w) {");
    vertexBuilder->codeAppend("  highp float tmp = subset.y;");
    vertexBuilder->codeAppend("  subset.y = subset.w;");
    vertexBuilder->codeAppend("  subset.w = tmp;");
    vertexBuilder->codeAppend("}");
    vertexBuilder->codeAppendf("%s = subset;", varying.vsOut().c_str());
    *args.outputSubset = varying.fsIn();
  }
}

}  // namespace tgfx
