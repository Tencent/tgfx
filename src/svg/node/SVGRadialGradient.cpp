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

#include "tgfx/svg/node/SVGRadialGradient.h"
#include <cstdint>
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

SkSVGRadialGradient::SkSVGRadialGradient() : INHERITED(SVGTag::kRadialGradient) {
}

bool SkSVGRadialGradient::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setCx(SVGAttributeParser::parse<SVGLength>("cx", name, value)) ||
         this->setCy(SVGAttributeParser::parse<SVGLength>("cy", name, value)) ||
         this->setR(SVGAttributeParser::parse<SVGLength>("r", name, value)) ||
         this->setFx(SVGAttributeParser::parse<SVGLength>("fx", name, value)) ||
         this->setFy(SVGAttributeParser::parse<SVGLength>("fy", name, value));
}

#ifndef RENDER_SVG
std::shared_ptr<Shader> SkSVGRadialGradient::onMakeShader(const SVGRenderContext& ctx,
                                                          const std::vector<Color>& colors,
                                                          const std::vector<float>& position,
                                                          TileMode, const Matrix&) const {
  SVGLengthContext lctx =
      this->getGradientUnits().type() == SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox
          ? SVGLengthContext({1, 1})
          : ctx.lengthContext();

  auto radius = lctx.resolve(fR, SVGLengthContext::LengthType::kOther);
  auto center = Point::Make(lctx.resolve(fCx, SVGLengthContext::LengthType::kHorizontal),
                            lctx.resolve(fCy, SVGLengthContext::LengthType::kVertical));

  // TODO(YGAurora): MakeTwoPointConical are unimplemented in tgfx
  // const auto focal = Point::Make(
  //     fFx.has_value() ? lctx.resolve(*fFx, SkSVGLengthContext::LengthType::kHorizontal) : center.x,
  //     fFy.has_value() ? lctx.resolve(*fFy, SkSVGLengthContext::LengthType::kVertical) : center.y);

  if (radius == 0) {
    const auto lastColor = colors.size() > 0 ? *colors.end() : Color::Black();
    return Shader::MakeColorShader(lastColor);
  }

  return Shader::MakeRadialGradient(center, radius, colors, position);
}
#endif
}  // namespace tgfx