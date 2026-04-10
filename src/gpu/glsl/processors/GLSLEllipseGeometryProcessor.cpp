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
    auto color = varyingHandler->addVarying("Color", SLType::Float4);
    colorVsOut = color.vsOut();
    colorFsIn = color.fsIn();
    if (args.gpVaryings) {
      args.gpVaryings->add("Color", colorFsIn);
    }
  }

  std::string positionName = "position";
  if (args.skipVertexCode) {
    static const std::string kEllipseGPVert = R"GLSL(
void TGFX_EllipseGP_VS(vec2 inPosition, vec2 inEllipseOffset, vec4 inEllipseRadii,
#ifndef TGFX_GP_ELLIPSE_COMMON_COLOR
                        vec4 inColor, out vec4 vColor,
#endif
                        out vec2 vEllipseOffsets, out vec4 vEllipseRadii,
                        out vec2 position) {
    vEllipseOffsets = inEllipseOffset;
    vEllipseRadii = inEllipseRadii;
#ifndef TGFX_GP_ELLIPSE_COMMON_COLOR
    vColor = inColor;
#endif
    position = inPosition;
}
)GLSL";
    vertBuilder->addFunction(kEllipseGPVert);
    vertBuilder->codeAppendf("highp vec2 %s;", positionName.c_str());
    std::string call = "TGFX_EllipseGP_VS(" + std::string(inPosition.name()) + ", " +
                       std::string(inEllipseOffset.name()) + ", " +
                       std::string(inEllipseRadii.name());
    if (!commonColor.has_value()) {
      call += ", " + std::string(inColor.name()) + ", " + colorVsOut;
    }
    call +=
        ", " + ellipseOffsets.vsOut() + ", " + ellipseRadii.vsOut() + ", " + positionName + ");";
    vertBuilder->codeAppend(call);
  } else {
    vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                             inEllipseOffset.name().c_str());
    vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(),
                             inEllipseRadii.name().c_str());
    if (!commonColor.has_value()) {
      vertBuilder->codeAppendf("%s = %s;", colorVsOut.c_str(), inColor.name().c_str());
    }
  }

  // Setup position
  args.vertBuilder->emitNormalizedPosition(args.skipVertexCode ? positionName
                                                               : std::string(inPosition.name()));
  // emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));

  if (!args.skipFragmentCode) {
    auto fragBuilder = args.fragBuilder;

    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorFsIn.c_str());

    fragBuilder->codeAppendf("vec2 offset = %s.xy;", ellipseOffsets.fsIn().c_str());
    if (stroke) {
      fragBuilder->codeAppendf("offset *= %s.xy;", ellipseRadii.fsIn().c_str());
    }
    fragBuilder->codeAppend("float test = dot(offset, offset) - 1.0;");
    fragBuilder->codeAppendf("vec2 grad = 2.0*offset*%s.xy;", ellipseRadii.fsIn().c_str());
    fragBuilder->codeAppend("float grad_dot = dot(grad, grad);");
    fragBuilder->codeAppend("grad_dot = max(grad_dot, 1.1755e-38);");
    fragBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
    fragBuilder->codeAppend("float edgeAlpha = clamp(0.5-test*invlen, 0.0, 1.0);");

    // for inner curve
    if (stroke) {
      fragBuilder->codeAppendf("offset = %s.xy*%s.zw;", ellipseOffsets.fsIn().c_str(),
                               ellipseRadii.fsIn().c_str());
      fragBuilder->codeAppend("test = dot(offset, offset) - 1.0;");
      fragBuilder->codeAppendf("grad = 2.0*offset*%s.zw;", ellipseRadii.fsIn().c_str());
      fragBuilder->codeAppend("grad_dot = dot(grad, grad);");
      fragBuilder->codeAppend("invlen = inversesqrt(grad_dot);");
      fragBuilder->codeAppend("edgeAlpha *= clamp(0.5+test*invlen, 0.0, 1.0);");
    }

    fragBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.outputCoverage.c_str());
  }
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
