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

#include "GLSLRectRoundStrokeGeometryProcessor.h"

namespace tgfx {
PlacementPtr<RectRoundStrokeGeometryProcessor> RectRoundStrokeGeometryProcessor::Make(
    BlockBuffer* buffer, AAType aaType, std::optional<Color> commonColor,
    std::optional<Matrix> uvMatrix) {
  return buffer->make<GLSLRectRoundStrokeGeometryProcessor>(aaType, commonColor, uvMatrix);
}

GLSLRectRoundStrokeGeometryProcessor::GLSLRectRoundStrokeGeometryProcessor(
    AAType aaType, std::optional<Color> commonColor, std::optional<Matrix> uvMatrix)
    : RectRoundStrokeGeometryProcessor(aaType, commonColor, uvMatrix) {
}

void GLSLRectRoundStrokeGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto& uvCoordsVar = inUVCoord.empty() ? inPosition : inUVCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  Varying ellipseRadii;
  if (aaType == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), inCoverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
    ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float2);
    vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(),
                             inEllipseRadii.name().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
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

  fragBuilder->codeAppendf("vec2 offset = %s;", ellipseOffsets.fsIn().c_str());
  if (aaType == AAType::Coverage) {
    fragBuilder->codeAppend("float test = dot(offset, offset) - 1.0;");
    fragBuilder->codeAppend("if (test > -0.5) {");
    fragBuilder->codeAppendf("vec2 grad = 2.0 * offset * %s;", ellipseRadii.fsIn().c_str());
    fragBuilder->codeAppend("float grad_dot = dot(grad, grad);");
    fragBuilder->codeAppend("grad_dot = max(grad_dot, 1.1755e-38);");
    fragBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
    fragBuilder->codeAppend("float edgeAlpha = clamp(0.5 - test * invlen, 0.0, 1.0);");
    fragBuilder->codeAppendf("%s *= edgeAlpha;", args.outputCoverage.c_str());
    fragBuilder->codeAppendf("}");
  } else {
    fragBuilder->codeAppend("float test = dot(offset, offset);");
    fragBuilder->codeAppend("float edgeAlpha = step(test, 1.0);");
    fragBuilder->codeAppendf("%s *= edgeAlpha;", args.outputCoverage.c_str());
  }

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(inPosition.name());
}

void GLSLRectRoundStrokeGeometryProcessor::setData(UniformData* vertexUniformData,
                                                   UniformData* fragmentUniformData,
                                                   FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}

}  // namespace tgfx
