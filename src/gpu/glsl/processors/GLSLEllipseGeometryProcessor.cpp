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
    BlockBuffer* buffer, int width, int height, bool stroke, bool useScale,
    std::optional<Color> commonColor) {
  return buffer->make<GLSLEllipseGeometryProcessor>(width, height, stroke, useScale, commonColor);
}

GLSLEllipseGeometryProcessor::GLSLEllipseGeometryProcessor(int width, int height, bool stroke,
                                                           bool useScale,
                                                           std::optional<Color> commonColor)
    : EllipseGeometryProcessor(width, height, stroke, useScale, commonColor) {
}

void GLSLEllipseGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  // emit attributes
  varyingHandler->emitAttributes(*this);

  auto offsetType = useScale ? SLType::Float3 : SLType::Float2;
  auto ellipseOffsets = varyingHandler->addVarying("EllipseOffsets", offsetType);
  vertBuilder->codeAppendf("%s = %s;", ellipseOffsets.vsOut().c_str(),
                           inEllipseOffset.name().c_str());

  auto ellipseRadii = varyingHandler->addVarying("EllipseRadii", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", ellipseRadii.vsOut().c_str(), inEllipseRadii.name().c_str());

  auto fragBuilder = args.fragBuilder;
  // setup pass through color
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
  // emit transforms
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(inPosition));
  // For stroked ellipses, we use the full ellipse equation (x^2/a^2 + y^2/b^2 = 1)
  // to compute both the edges because we need two separate test equations for
  // the single offset.
  // For filled ellipses we can use a unit circle equation (x^2 + y^2 = 1), and warp
  // the distance by the gradient, non-uniformly scaled by the inverse of the
  // ellipse size.

  // On medium precision devices, we scale the denominator of the distance equation
  // before taking the inverse square root to minimize the chance that we're dividing
  // by zero, then we scale the result back.

  // for outer curve
  fragBuilder->codeAppendf("vec2 offset = %s.xy;", ellipseOffsets.fsIn().c_str());
  if (stroke) {
    fragBuilder->codeAppendf("offset *= %s.xy;", ellipseRadii.fsIn().c_str());
  }
  fragBuilder->codeAppend("float test = dot(offset, offset) - 1.0;");
  if (useScale) {
    fragBuilder->codeAppendf("vec2 grad = 2.0*offset*(%s.z*%s.xy);", ellipseOffsets.fsIn().c_str(),
                             ellipseRadii.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("vec2 grad = 2.0*offset*%s.xy;", ellipseRadii.fsIn().c_str());
  }
  fragBuilder->codeAppend("float grad_dot = dot(grad, grad);");

  // avoid calling inversesqrt on zero.
  if (args.caps->floatIs32Bits) {
    fragBuilder->codeAppend("grad_dot = max(grad_dot, 1.1755e-38);");
  } else {
    fragBuilder->codeAppend("grad_dot = max(grad_dot, 6.1036e-5);");
  }
  if (useScale) {
    fragBuilder->codeAppendf("float invlen = %s.z*inversesqrt(grad_dot);",
                             ellipseOffsets.fsIn().c_str());
  } else {
    fragBuilder->codeAppend("float invlen = inversesqrt(grad_dot);");
  }
  fragBuilder->codeAppend("float edgeAlpha = clamp(0.5-test*invlen, 0.0, 1.0);");

  // for inner curve
  if (stroke) {
    fragBuilder->codeAppendf("offset = %s.xy*%s.zw;", ellipseOffsets.fsIn().c_str(),
                             ellipseRadii.fsIn().c_str());
    fragBuilder->codeAppend("test = dot(offset, offset) - 1.0;");
    if (useScale) {
      fragBuilder->codeAppendf("grad = 2.0*offset*(%s.z*%s.zw);", ellipseOffsets.fsIn().c_str(),
                               ellipseRadii.fsIn().c_str());
    } else {
      fragBuilder->codeAppendf("grad = 2.0*offset*%s.zw;", ellipseRadii.fsIn().c_str());
    }
    fragBuilder->codeAppend("grad_dot = dot(grad, grad);");
    if (!args.caps->floatIs32Bits) {
      fragBuilder->codeAppend("grad_dot = max(grad_dot, 6.1036e-5);");
    }
    if (useScale) {
      fragBuilder->codeAppendf("invlen = %s.z*inversesqrt(grad_dot);",
                               ellipseOffsets.fsIn().c_str());
    } else {
      fragBuilder->codeAppend("invlen = inversesqrt(grad_dot);");
    }
    fragBuilder->codeAppend("edgeAlpha *= clamp(0.5+test*invlen, 0.0, 1.0);");
  }

  fragBuilder->codeAppendf("%s = vec4(edgeAlpha);", args.outputCoverage.c_str());
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
