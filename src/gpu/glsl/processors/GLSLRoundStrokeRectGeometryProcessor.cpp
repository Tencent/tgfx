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

#include "GLSLRoundStrokeRectGeometryProcessor.h"

namespace tgfx {
PlacementPtr<RoundStrokeRectGeometryProcessor> RoundStrokeRectGeometryProcessor::Make(
    BlockAllocator* allocator, AAType aaType, std::optional<PMColor> commonColor,
    std::optional<Matrix> uvMatrix) {
  return allocator->make<GLSLRoundStrokeRectGeometryProcessor>(aaType, commonColor, uvMatrix);
}

GLSLRoundStrokeRectGeometryProcessor::GLSLRoundStrokeRectGeometryProcessor(
    AAType aaType, std::optional<PMColor> commonColor, std::optional<Matrix> uvMatrix)
    : RoundStrokeRectGeometryProcessor(aaType, commonColor, uvMatrix) {
}

void GLSLRoundStrokeRectGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto& uvCoordsVar = inUVCoord.empty() ? inPosition : inUVCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  Varying ellipseRadii;
  Varying coverageVar;
  if (aaType == AAType::Coverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), inCoverage.name().c_str());
    ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(),
                             inEllipseRadii.name().c_str());
  }

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                           inEllipseOffset.name().c_str());

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }

  fragBuilder->codeAppendf("highp vec2 offset = %s;", ellipseOffsets.fsIn().c_str());
  // The shared SDF scales the per-vertex coverage inside the ramp band when AA is on, and is a
  // hard inside step otherwise. The AA form rides the function's runtime flag as a literal, so the
  // driver folds the arm this draw does not use.
  auto coverageEntry = fragBuilder->appendFunction("roundStrokeRectCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (aaType == AAType::Coverage) {
    fragBuilder->codeAppendf("%s = vec4(%s(offset, %s, %s, 1));", args.outputCoverage.c_str(),
                             coverageEntry.c_str(), ellipseRadii.fsIn().c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(%s(offset, vec2(0.0), 1.0, 0));",
                             args.outputCoverage.c_str(), coverageEntry.c_str());
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(inPosition.name());
}

void GLSLRoundStrokeRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                                   UniformData* fragmentUniformData,
                                                   FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}

}  // namespace tgfx
