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
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto& uvCoordsVar = inUVCoord.empty() ? inPosition : inUVCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  std::string coverageFsIn;
  std::string coverageVsOut;
  std::string ellipseRadiiFsIn;
  std::string ellipseRadiiVsOut;
  Varying ellipseRadii;
  if (aaType == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    coverageVsOut = coverageVar.vsOut();
    coverageFsIn = coverageVar.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Coverage", coverageFsIn);
    }
    ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float2);
    ellipseRadiiVsOut = ellipseRadii.vsOut();
    ellipseRadiiFsIn = ellipseRadii.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("EllipseRadii", ellipseRadiiFsIn);
    }
  }

  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", SLType::Float2);
  if (args.gpVaryings) {
    args.gpVaryings->add("EllipseOffsets", ellipseOffsets.fsIn());
  }

  std::string colorFsIn;
  std::string colorVsOut;
  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    colorFsIn = colorName;
    if (args.gpUniforms) {
      args.gpUniforms->add("Color", colorName);
    }
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    colorVsOut = colorVar.vsOut();
    colorFsIn = colorVar.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorFsIn);
    }
  }

  std::string positionName = "position";
  static const std::string kRoundStrokeRectGPVert = R"GLSL(
void TGFX_RoundStrokeRectGP_VS(vec2 inPosition, vec2 inEllipseOffset,
#ifdef TGFX_GP_RRECT_COVERAGE_AA
                                 float inCoverage, vec2 inEllipseRadii,
                                 out float vCoverage, out vec2 vEllipseRadii,
#endif
#ifndef TGFX_GP_RRECT_COMMON_COLOR
                                 vec4 inColor, out vec4 vColor,
#endif
                                 out vec2 vEllipseOffsets, out vec2 position) {
    vEllipseOffsets = inEllipseOffset;
#ifdef TGFX_GP_RRECT_COVERAGE_AA
    vCoverage = inCoverage;
    vEllipseRadii = inEllipseRadii;
#endif
#ifndef TGFX_GP_RRECT_COMMON_COLOR
    vColor = inColor;
#endif
    position = inPosition;
}
)GLSL";
  vertBuilder->addFunction(kRoundStrokeRectGPVert);
  vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
  std::string call = "TGFX_RoundStrokeRectGP_VS(" + std::string(inPosition.name()) + ", " +
                     std::string(inEllipseOffset.name());
  if (aaType == AAType::Coverage) {
    call += ", " + std::string(inCoverage.name()) + ", " + std::string(inEllipseRadii.name()) +
            ", " + coverageVsOut + ", " + ellipseRadiiVsOut;
  }
  if (!commonColor.has_value()) {
    call += ", " + std::string(inColor.name()) + ", " + colorVsOut;
  }
  call += ", " + ellipseOffsets.vsOut() + ", " + positionName + ");";
  vertBuilder->codeAppend(call);

  args.vertBuilder->emitNormalizedPosition(positionName);
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
