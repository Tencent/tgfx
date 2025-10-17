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

  auto innerRectVar = varyingHandler->addVarying("InnerRect", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", innerRectVar.vsOut().c_str(), inInnerRect.name().c_str());

  auto radiusVar = varyingHandler->addVarying("CornerRadius", SLType::Float);
  vertBuilder->codeAppendf("%s = %s;", radiusVar.vsOut().c_str(), inCornerRadius.name().c_str());

  auto positionVar = varyingHandler->addVarying("Position", SLType::Float2);
  vertBuilder->codeAppendf("%s = %s;", positionVar.vsOut().c_str(), uvCoordsVar.name().c_str());

  if (aaType == AAType::Coverage) {
    auto coverageVar = varyingHandler->addVarying("Coverage", SLType::Float);
    vertBuilder->codeAppendf("%s = %s;", coverageVar.vsOut().c_str(), inCoverage.name().c_str());
    fragBuilder->codeAppendf("%s = vec4(%s);", args.outputCoverage.c_str(),
                             coverageVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("%s = vec4(1.0);", args.outputCoverage.c_str());
  }

  if (commonColor.has_value()) {
    auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }

  //round corner coverage process
  auto rectVarStr = innerRectVar.fsIn().c_str();
  fragBuilder->codeAppend("const float epsilon = 0.01;");
  fragBuilder->codeAppendf("if(%s > epsilon) {", radiusVar.fsIn().c_str());
  fragBuilder->codeAppendf("vec2 leftTop = %s.xy;", rectVarStr);
  fragBuilder->codeAppendf("vec2 rightBottom = %s.zw;", rectVarStr);
  fragBuilder->codeAppendf("vec2 s = step(%s, leftTop) + step(rightBottom, %s);",
                           positionVar.fsIn().c_str(), positionVar.fsIn().c_str());
  fragBuilder->codeAppend("bool inSideCornerBox = s.x * s.y > 1.0 - epsilon;");
  fragBuilder->codeAppend("if(inSideCornerBox) {");
  fragBuilder->codeAppendf("float l = %s.x; float t = %s.y; float r = %s.z; float b = %s.w;",
                           rectVarStr, rectVarStr, rectVarStr, rectVarStr);
  fragBuilder->codeAppend("vec2 CORNERS[4] = vec2[4](vec2(l,t), vec2(l,b), vec2(r,t), vec2(r,b));");
  fragBuilder->codeAppendf("int index = %s.x <= l ? 0 : 2;", positionVar.fsIn().c_str());
  fragBuilder->codeAppendf("index += %s.y <= t ? 0 : 1;", positionVar.fsIn().c_str());
  fragBuilder->codeAppend("vec2 center = CORNERS[index];");
  fragBuilder->codeAppendf("float dist = length(%s - center);", positionVar.fsIn().c_str());
  fragBuilder->codeAppendf("float smoothing = min(0.5, %s);", radiusVar.fsIn().c_str());
  if (aaType == AAType::Coverage) {
    fragBuilder->codeAppendf(
        "float alpha = 1.0 - smoothstep(%s - smoothing, %s + smoothing, dist);",
        radiusVar.fsIn().c_str(), radiusVar.fsIn().c_str());
  } else {
    fragBuilder->codeAppendf("float alpha = step(dist, %s);", radiusVar.fsIn().c_str());
  }
  fragBuilder->codeAppendf("%s *= alpha;", args.outputCoverage.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("}");

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
