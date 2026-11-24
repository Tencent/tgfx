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

  auto& uvCoordsVar = inUVCoord.empty() ? inPosition : inUVCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  Varying ellipseRadii;
  Varying coverageVar;
  if (aaType == AAType::Coverage) {
    coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->storeVarying(coverageVar.vsOut(), inCoverage.name());
    ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float2);
    vertBuilder->storeVarying(ellipseRadii.vsOut(), inEllipseRadii.name());
  }

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
  vertBuilder->storeVarying(ellipseOffsets.vsOut(), inEllipseOffset.name());

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorName);
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->storeVarying(colorVar.vsOut(), inColor.name());
    fragBuilder->storeColor(args.outputColor, colorVar.fsIn());
  }

  fragBuilder->aliasValue("offset", ellipseOffsets.fsIn(), "highp vec2");
  // The shared SDF scales the per-vertex coverage inside the ramp band when AA is on, and is a
  // hard inside step otherwise. The AA form rides the function's runtime flag as a literal, so the
  // driver folds the arm this draw does not use.
  auto coverageEntry = fragBuilder->appendFunction("roundStrokeRectCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (aaType == AAType::Coverage) {
    fragBuilder->appendCall(args.outputCoverage, coverageEntry,
                            {"offset", ellipseRadii.fsIn(), coverageVar.fsIn(), "1"}, "vec4");
  } else {
    fragBuilder->appendCall(args.outputCoverage, coverageEntry, {"offset", "vec2(0.0)", "1.0", "0"},
                            "vec4");
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->storePosition(inPosition.name());
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
