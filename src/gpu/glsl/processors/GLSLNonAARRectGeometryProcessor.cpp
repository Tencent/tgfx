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

#include "GLSLNonAARRectGeometryProcessor.h"

namespace tgfx {
PlacementPtr<NonAARRectGeometryProcessor> NonAARRectGeometryProcessor::Make(
    BlockAllocator* allocator, int width, int height, bool stroke,
    std::optional<PMColor> commonColor) {
  return allocator->make<GLSLNonAARRectGeometryProcessor>(width, height, stroke, commonColor);
}

GLSLNonAARRectGeometryProcessor::GLSLNonAARRectGeometryProcessor(int width, int height, bool stroke,
                                                                 std::optional<PMColor> commonColor)
    : NonAARRectGeometryProcessor(width, height, stroke, commonColor) {
}

void GLSLNonAARRectGeometryProcessor::emitCode(EmitArgs& args) const {
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit attributes
  varyingHandler->emitAttributes(*this);

  // Setup color output
  if (commonColor.has_value()) {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", color.fsIn());
    }
  }

  // Pass local coordinates to fragment shader
  auto localCoordVarying = varyingHandler->addVarying("localCoord", SLType::Float2);
  if (args.gpVaryings) {
    args.gpVaryings->add("localCoord", localCoordVarying.fsIn());
  }

  // Pass radii to fragment shader
  auto radiiVarying = varyingHandler->addVarying("radii", SLType::Float2);
  if (args.gpVaryings) {
    args.gpVaryings->add("radii", radiiVarying.fsIn());
  }

  // Pass rect bounds to fragment shader
  auto boundsVarying = varyingHandler->addVarying("rectBounds", SLType::Float4);
  if (args.gpVaryings) {
    args.gpVaryings->add("rectBounds", boundsVarying.fsIn());
  }

  // Pass stroke width to fragment shader (stroke mode only)
  if (stroke) {
    auto strokeWidthVarying = varyingHandler->addVarying("strokeWidth", SLType::Float2);
    if (args.gpVaryings) {
      args.gpVaryings->add("strokeWidth", strokeWidthVarying.fsIn());
    }
  }

  // Register coord transforms (VS code emitted later by ModularProgramBuilder via
  // emitCoordTransformCode, after the VS call expression is appended).
  registerCoordTransforms(args, varyingHandler, uniformHandler);
}

void GLSLNonAARRectGeometryProcessor::setData(UniformData* vertexUniformData,
                                              UniformData* fragmentUniformData,
                                              FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(Matrix::I(), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}
}  // namespace tgfx
