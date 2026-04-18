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

#include "GLSLEllipseGeometryProcessor.h"

namespace tgfx {
PlacementPtr<EllipseGeometryProcessor> EllipseGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLEllipseGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLEllipseGeometryProcessor::GLSLEllipseGeometryProcessor(int width, int height, bool stroke,
                                                           std::optional<PMColor> commonColor)
    : EllipseGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLEllipseGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // emit attributes
  varyingHandler->emitAttributes(*this);

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
  if (args.gpVaryings) {
    args.gpVaryings->add("EllipseOffsets", ellipseOffsets.fsIn());
  }

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);
  if (args.gpVaryings) {
    args.gpVaryings->add("EllipseRadii", ellipseRadii.fsIn());
  }

  // setup pass through color
  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", color.fsIn());
    }
  }

  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));
}

void GLSLEllipseGeometryProcessor::setData(UniformData* vertexUniformData,
                                           UniformData* fragmentUniformData,
                                           FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
