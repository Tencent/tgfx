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
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // Emit attributes
  varyingHandler->emitAttributes(*this);

  // Setup color output
  std::string colorFsIn;
  std::string colorVsOut;
  if (commonColor.has_value()) {
    auto colorName =
        uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    colorFsIn = colorName;
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    colorVsOut = color.vsOut();
    colorFsIn = color.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorFsIn);
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
  Varying strokeWidthVarying;
  if (stroke) {
    strokeWidthVarying = varyingHandler->addVarying("strokeWidth", SLType::Float2);
    if (args.gpVaryings) {
      args.gpVaryings->add("strokeWidth", strokeWidthVarying.fsIn());
    }
  }

  std::string positionName = "position";
  static const std::string kNonAARRectGPVert = R"GLSL(
void TGFX_NonAARRectGP_VS(vec2 inPosition, vec2 inLocalCoord, vec2 inRadii, vec4 inRectBounds,
#ifndef TGFX_GP_NONAA_COMMON_COLOR
                           vec4 inColor, out vec4 vColor,
#endif
#ifdef TGFX_GP_NONAA_STROKE
                           vec2 inStrokeWidth, out vec2 vStrokeWidth,
#endif
                           out vec2 vLocalCoord, out vec2 vRadii, out vec4 vRectBounds,
                           out vec2 position) {
    position = inPosition;
    vLocalCoord = inLocalCoord;
    vRadii = inRadii;
    vRectBounds = inRectBounds;
#ifndef TGFX_GP_NONAA_COMMON_COLOR
    vColor = inColor;
#endif
#ifdef TGFX_GP_NONAA_STROKE
    vStrokeWidth = inStrokeWidth;
#endif
}
)GLSL";
  vertBuilder->addFunction(kNonAARRectGPVert);
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_NonAARRectGP_VS(" + std::string(inPosition.name()) + ", " +
                     std::string(inLocalCoord.name()) + ", " + std::string(inRadii.name()) + ", " +
                     std::string(inRectBounds.name());
  if (!commonColor.has_value()) {
    call += ", " + std::string(inColor.name()) + ", " + colorVsOut;
  }
  if (stroke) {
    call += ", " + std::string(inStrokeWidth.name()) + ", " + strokeWidthVarying.vsOut();
  }
  call += ", " + localCoordVarying.vsOut() + ", " + radiiVarying.vsOut() + ", " +
          boundsVarying.vsOut() + ", " + positionName + ");";
  vertBuilder->codeAppend(call);

  // Output position using RTAdjust uniform
  vertBuilder->emitNormalizedPosition(positionName);

  // Emit transforms using position as UV coordinates.
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler,
                 ShaderVar(inPosition.name(), SLType::Float2));
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
