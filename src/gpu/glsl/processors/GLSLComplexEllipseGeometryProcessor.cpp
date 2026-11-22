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
  vertBuilder->storeVarying(ellipseOffsets.vsOut(), inEllipseOffset.name());

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);
  vertBuilder->storeVarying(ellipseRadii.vsOut(), inEllipseRadii.name());

  auto edgeDist = varyingHandler->addVarying("EdgeDist", SLType::Float2);
  vertBuilder->storeVarying(edgeDist.vsOut(), inEdgeDist.name());

  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("StrokeWidth", SLType::Float2);
    vertBuilder->storeVarying(strokeWidthVarying.vsOut(), inStrokeWidth.name());
  }

  auto fragBuilder = args.fragBuilder;
  // Setup pass through color
  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->storeColor(args.outputColor, colorName);
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->storeVarying(color.vsOut(), inColor.name());
    fragBuilder->storeColor(args.outputColor, color.fsIn());
  }

  // Setup position
  args.vertBuilder->storePosition(inPosition.name());
  // Emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));

  // Fragment shader: the shared offset-based region dispatch. The stroke form rides the
  // function's runtime flag as a literal, so the driver folds the arm this draw does not use.
  auto coverageEntry = fragBuilder->appendFunction("complexEllipseEdgeCoverage");
  if (coverageEntry.empty()) {
    return;
  }
  if (stroke) {
    fragBuilder->aliasValue("sw", strokeWidthVarying.fsIn(), "highp vec2");
    fragBuilder->appendCall(args.outputCoverage, coverageEntry,
                            {ellipseOffsets.fsIn() + ".xy", ellipseRadii.fsIn(), edgeDist.fsIn(),
                             strokeWidthVarying.fsIn(), "1"},
                            "vec4");
  } else {
    fragBuilder->appendCall(
        args.outputCoverage, coverageEntry,
        {ellipseOffsets.fsIn() + ".xy", ellipseRadii.fsIn(), edgeDist.fsIn(), "vec2(0.0)", "0"},
        "vec4");
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
