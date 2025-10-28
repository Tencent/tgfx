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

#include "GLSLRectStrokeGeometryProcessor.h"

namespace tgfx {
PlacementPtr<RectStrokeGeometryProcessor> RectStrokeGeometryProcessor::Make(
    BlockBuffer* buffer, AAType aaType, std::optional<Color> commonColor,
    std::optional<Matrix> uvMatrix) {
  return buffer->make<GLSLRectStrokeGeometryProcessor>(aaType, commonColor, uvMatrix);
}

GLSLRectStrokeGeometryProcessor::GLSLRectStrokeGeometryProcessor(AAType aaType,
                                                                 std::optional<Color> commonColor,
                                                                 std::optional<Matrix> uvMatrix)
    : RectStrokeGeometryProcessor(aaType, commonColor, uvMatrix) {
}

static void InsertBevelCode(FragmentShaderBuilder* fragBuilder, AAType aaType) {
  fragBuilder->codeAppend("float ox = halfWidthX;");
  fragBuilder->codeAppend("float oy = halfWidthY;");
  if (aaType == AAType::Coverage) {
    fragBuilder->codeAppend("ox += min(0.5, ox);");
    fragBuilder->codeAppend("oy += min(0.5, oy);");
  }
  fragBuilder->codeAppend(
      "vec2 Pts1[4] = vec2[4](vec2(l - ox, t), vec2(l - ox, b), vec2(r, t - oy), "
      "vec2(r, b + oy));");
  fragBuilder->codeAppend(
      "vec2 Pts2[4] = vec2[4](vec2(l, t - oy), vec2(l, b + oy), vec2(r + ox, t), "
      "vec2(r + ox, b));");
  fragBuilder->codeAppend("//pointInTriangle;");
  fragBuilder->codeAppend("vec2 p0 = corners[index];");
  fragBuilder->codeAppend("vec2 p1 = Pts1[index];");
  fragBuilder->codeAppend("vec2 p2 = Pts2[index];");
  fragBuilder->codeAppend(
      "float c1 = (p1.x - p0.x) * (p.y - p0.y) - (p1.y - p0.y) * (p.x - p0.x);");
  fragBuilder->codeAppend(
      "float c2 = (p2.x - p1.x) * (p.y - p1.y) - (p2.y - p1.y) * (p.x - p1.x);");
  fragBuilder->codeAppend(
      "float c3 = (p0.x - p2.x) * (p.y - p2.y) - (p0.y - p2.y) * (p.x - p2.x);");
  fragBuilder->codeAppend(
      "bool isInTriangle = (c1 >= 0.0 && c2 >= 0.0 && c3 >= 0.0) || (c1 <= 0.0 && c2 <= 0.0 && c3 "
      "<= 0.0);");
  if (aaType == AAType::Coverage) {
    fragBuilder->codeAppend("if(isInTriangle) {");
    fragBuilder->codeAppend("//compute distance from point to line segment;");
    fragBuilder->codeAppend("vec2 ab = p2 - p1;");
    fragBuilder->codeAppend("vec2 ap = p - p1;");
    fragBuilder->codeAppend("float dist =  abs((ab.x * ap.y - ab.y * ap.x)) / length(ab);");
    fragBuilder->codeAppend("alpha = clamp(dist, 0.0, 1.0);");
    fragBuilder->codeAppend("}");
  } else {
    fragBuilder->codeAppend("alpha = float(isInTriangle);");
  }
}

static void InsertRoundCode(FragmentShaderBuilder* fragBuilder, AAType aaType) {
  fragBuilder->codeAppend("vec2 center = corners[index];");
  fragBuilder->codeAppend("float ox = cornerOffset.x;");
  fragBuilder->codeAppend("float oy = cornerOffset.y;");
  fragBuilder->codeAppend(
      "vec2 offset[4] = vec2[4](vec2(-ox, -oy), vec2(-ox, oy), vec2(ox, -oy), vec2(ox, oy));");
  fragBuilder->codeAppend("center += offset[index];");
  fragBuilder->codeAppend("float dist = length(p - center);");
  fragBuilder->codeAppend("float cornerRadius = minHalfWidth;");
  if (aaType == AAType::Coverage) {
    fragBuilder->codeAppend("float smoothing = min(0.5, cornerRadius);");
    fragBuilder->codeAppend(
        "alpha = 1.0 - smoothstep(cornerRadius - smoothing, cornerRadius + smoothing, dist);");
  } else {
    fragBuilder->codeAppend("alpha = step(dist, cornerRadius);");
  }
  fragBuilder->codeAppend("}");
}

void GLSLRectStrokeGeometryProcessor::emitCode(EmitArgs& args) const {
  auto vertBuilder = args.vertBuilder;
  auto fragBuilder = args.fragBuilder;
  auto varyingHandler = args.varyingHandler;
  auto uniformHandler = args.uniformHandler;

  varyingHandler->emitAttributes(*this);

  auto& uvCoordsVar = inUVCoord.empty() ? inPosition : inUVCoord;
  emitTransforms(args, vertBuilder, varyingHandler, uniformHandler, ShaderVar(uvCoordsVar));

  const auto innerRectVar = varyingHandler->addVarying("InnerRect", SLType::Float4);
  vertBuilder->codeAppendf("%s = %s;", innerRectVar.vsOut().c_str(), inInnerRect.name().c_str());

  const auto strokeWidthVar = varyingHandler->addVarying("StrokeWidth", SLType::Float2, true);
  vertBuilder->codeAppendf("%s = %s;", strokeWidthVar.vsOut().c_str(),
                           inStrokeWidth.name().c_str());

  const auto strokeJoinVar = varyingHandler->addVarying("StrokeJoin", SLType::Float, true);
  vertBuilder->codeAppendf("%s = %s;", strokeJoinVar.vsOut().c_str(), inStrokeJoin.name().c_str());

  const auto positionVar = varyingHandler->addVarying("Position", SLType::Float2);
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
    const auto colorName =
        args.uniformHandler->addUniform("Color", UniformFormat::Float4, ShaderStage::Fragment);
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorName.c_str());
  } else {
    const auto colorVar = varyingHandler->addVarying("Color", SLType::Float4);
    vertBuilder->codeAppendf("%s = %s;", colorVar.vsOut().c_str(), inColor.name().c_str());
    fragBuilder->codeAppendf("%s = %s;", args.outputColor.c_str(), colorVar.fsIn().c_str());
  }

  auto rectVarStr = innerRectVar.fsIn().c_str();
  auto strokeWidthStr = strokeWidthVar.fsIn().c_str();
  fragBuilder->codeAppend("const float epsilon = 0.01;");
  fragBuilder->codeAppendf("float halfWidthX = %s.x * 0.5;", strokeWidthStr);
  fragBuilder->codeAppendf("float halfWidthY = %s.y * 0.5;", strokeWidthStr);
  fragBuilder->codeAppend("float minHalfWidth = min(halfWidthX, halfWidthY);");
  fragBuilder->codeAppendf("int joinType = int(%s);", strokeJoinVar.fsIn().c_str());
  fragBuilder->codeAppend("if(joinType > 0 && minHalfWidth > epsilon) {");
  fragBuilder->codeAppendf("vec2 leftTop = %s.xy;", rectVarStr);
  fragBuilder->codeAppendf("vec2 rightBottom = %s.zw;", rectVarStr);
  fragBuilder->codeAppend("vec2 cornerOffset = vec2(0.0, 0.0);");
  fragBuilder->codeAppend("if (joinType == 1 && abs(halfWidthX - halfWidthY) > epsilon) {");
  fragBuilder->codeAppend(
      "cornerOffset = vec2(halfWidthX - minHalfWidth, halfWidthY - minHalfWidth);");
  fragBuilder->codeAppend("leftTop -= cornerOffset;");
  fragBuilder->codeAppend("rightBottom += cornerOffset;");
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppendf("vec2 s = step(%s, leftTop) + step(rightBottom, %s);",
                           positionVar.fsIn().c_str(), positionVar.fsIn().c_str());
  fragBuilder->codeAppend("bool inSideCornerBox = s.x * s.y > 1.0 - epsilon;");
  fragBuilder->codeAppend("if(inSideCornerBox) {");
  fragBuilder->codeAppendf("float l = %s.x;", rectVarStr);
  fragBuilder->codeAppendf("float t = %s.y;", rectVarStr);
  fragBuilder->codeAppendf("float r = %s.z;", rectVarStr);
  fragBuilder->codeAppendf("float b = %s.w;", rectVarStr);
  fragBuilder->codeAppend(
      "vec2 corners[4] = vec2[4](vec2(l, t), vec2(l, b), vec2(r, t), vec2(r, b));");
  fragBuilder->codeAppendf("int index = %s.x <= l ? 0 : 2;", positionVar.fsIn().c_str());
  fragBuilder->codeAppendf("index += %s.y <= t ? 0 : 1;", positionVar.fsIn().c_str());
  fragBuilder->codeAppend("float alpha = 0.0;");
  fragBuilder->codeAppendf("vec2 p = %s;", positionVar.fsIn().c_str());
  fragBuilder->codeAppend("if(joinType == 1) {//Round-join;");
  InsertRoundCode(fragBuilder, aaType);
  fragBuilder->codeAppend("else {//Bevel-join;");
  InsertBevelCode(fragBuilder, aaType);
  fragBuilder->codeAppend("}");

  fragBuilder->codeAppendf("%s *= alpha;", args.outputCoverage.c_str());
  fragBuilder->codeAppend("}");
  fragBuilder->codeAppend("}");

  // Emit the vertex position to the hardware in the normalized window coordinates it expects.
  args.vertBuilder->emitNormalizedPosition(inPosition.name());
}

void GLSLRectStrokeGeometryProcessor::setData(UniformData* vertexUniformData,
                                              UniformData* fragmentUniformData,
                                              FPCoordTransformIter* transformIter) const {
  setTransformDataHelper(uvMatrix.value_or(Matrix::I()), vertexUniformData, transformIter);
  if (commonColor.has_value()) {
    fragmentUniformData->setData("Color", *commonColor);
  }
}

}  // namespace tgfx
