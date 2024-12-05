/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/svg/node/SVGLinearGradient.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

SkSVGLinearGradient::SkSVGLinearGradient() : INHERITED(SVGTag::LinearGradient) {
}

bool SkSVGLinearGradient::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setX1(SVGAttributeParser::parse<SVGLength>("x1", name, value)) ||
         this->setY1(SVGAttributeParser::parse<SVGLength>("y1", name, value)) ||
         this->setX2(SVGAttributeParser::parse<SVGLength>("x2", name, value)) ||
         this->setY2(SVGAttributeParser::parse<SVGLength>("y2", name, value));
}

#ifndef RENDER_SVG
std::shared_ptr<Shader> SkSVGLinearGradient::onMakeShader(const SVGRenderContext& ctx,
                                                          const std::vector<Color>& colors,
                                                          const std::vector<float>& positions,
                                                          TileMode, const Matrix&) const {
  SVGLengthContext lctx = ctx.lengthContext();
  lctx.setPatternUnits(getGradientUnits());

  auto startPoint = Point::Make(lctx.resolve(fX1, SVGLengthContext::LengthType::Horizontal),
                                lctx.resolve(fY1, SVGLengthContext::LengthType::Vertical));
  auto endPoint = Point::Make(lctx.resolve(fX2, SVGLengthContext::LengthType::Horizontal),
                              lctx.resolve(fY2, SVGLengthContext::LengthType::Vertical));

  return Shader::MakeLinearGradient(startPoint, endPoint, colors, positions);
}
#endif
}  // namespace tgfx