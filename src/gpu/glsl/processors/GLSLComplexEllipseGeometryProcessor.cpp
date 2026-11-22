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

#include "GLSLComplexEllipseGeometryProcessor.h"

namespace tgfx {
PlacementPtr<ComplexEllipseGeometryProcessor> ComplexEllipseGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLComplexEllipseGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLComplexEllipseGeometryProcessor::GLSLComplexEllipseGeometryProcessor(
    int width, int height, bool stroke, std::optional<PMColor> commonColor)
    : ComplexEllipseGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLComplexEllipseGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit attributes
  varyingHandler->emitAttributes(*this);

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                           inEllipseOffset.name().c_str());

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(), inEllipseRadii.name().c_str());

  auto edgeDist = varyingHandler->addVarying("EdgeDist", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", edgeDist.vsOut().c_str(), inEdgeDist.name().c_str());

  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("StrokeWidth", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", strokeWidthVarying.vsOut().c_str(),
                             inStrokeWidth.name().c_str());
  }

  auto fragBuilder = args.fragBuilder;
  // Setup pass through color
  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", color.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), color.fsIn().c_str());
  }

  // Setup position
  args.vertBuilder->emitNormalizedPosition(inPosition.name());
  // Emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));

  // Fragment shader: the shared offset-based region dispatch. The stroke form rides the
  // function's runtime flag as a literal, so the driver folds the arm this draw does not use.
  auto coverageEntry = fragBuilder->appendFunction("complexEllipseEdgeCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (stroke) {
    fragBuilder->codeAppendf("highp vec2 sw = %s;", strokeWidthVarying.fsIn().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s(%s.xy, %s, %s, sw, 1));", args.outputCoverage.c_str(),
                             coverageEntry.c_str(), ellipseOffsets.fsIn().c_str(),
                             ellipseRadii.fsIn().c_str(), edgeDist.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(%s(%s.xy, %s, %s, vec2(0.0), 0));",
                             args.outputCoverage.c_str(), coverageEntry.c_str(),
                             ellipseOffsets.fsIn().c_str(), ellipseRadii.fsIn().c_str(),
                             edgeDist.fsIn().c_str());
  }
}

void GLSLComplexEllipseGeometryProcessor::setData(UniformData* vertexUniformData,
                                                  UniformData* fragmentUniformData,
                                                  FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
