/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLQuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {
PlacementPtr<QuadPerEdgeAAGeometryProcessor> QuadPerEdgeAAGeometryProcessor::Make(
    BlockBuffer* buffer, int width, int height, AAType aa, std::optional<Color> commonColor,
    std::optional<Matrix> uvMatrix, bool hasSubset) {
  return buffer->make<GLQuadPerEdgeAAGeometryProcessor>(width, height, aa, commonColor, uvMatrix,
                                                        hasSubset);
}

GLQuadPerEdgeAAGeometryProcessor::GLQuadPerEdgeAAGeometryProcessor(int width, int height, AAType aa,
                                                                   std::optional<Color> commonColor,
                                                                   std::optional<Matrix> uvMatrix,
                                                                   bool hasSubset)
    : QuadPerEdgeAAGeometryProcessor(width, height, aa, commonColor, uvMatrix, hasSubset) {
}

void GLQuadPerEdgeAAGeometryProcessor::emitCode(EmitArgs& args) const {
  auto* vertBuilder = args.vertBuilder;
  auto* fragBuilder = args.fragBuilder;
  auto* varyingHandler = args.varyingHandler;
  auto* uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto uvCoordsVar = uvCoord.isInitialized() ? uvCoord.asShaderVar() : position.asShaderVar();
  emitTransforms(vertBuilder, varyingHandler, uniformHandler, uvCoordsVar,
                 args.fpCoordTransformHandler);
  if (subset.isInitialized()) {
    auto varying = varyingHandler->addVarying("vtexsubset", SLType::Float4, true);
    vertBuilder->codeAppendf("%s = %s;", varying.vsOut().c_str(), subset.name().c_str());
  }

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
        args.uniformHandler->addUniform(ShaderFlags::Fragment, SLType::Float4, "Color");
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), color.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(position.name());
}

void GLQuadPerEdgeAAGeometryProcessor::setData(UniformBuffer* uniformBuffer,
                                               FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), uniformBuffer, transformIter);
  if (commonColor.has_value()) {
    uniformBuffer->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
