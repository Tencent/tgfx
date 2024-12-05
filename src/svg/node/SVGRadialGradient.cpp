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

SkSVGRadialGradient::SkSVGRadialGradient() : INHERITED(SVGTag::RadialGradient) {
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
                                                          TileMode, const Matrix& matrix) const {
  SVGLengthContext lctx = ctx.lengthContext();
  lctx.setPatternUnits(getGradientUnits());

  auto radius = lctx.resolve(fR, SVGLengthContext::LengthType::Other);
  auto center = Point::Make(lctx.resolve(fCx, SVGLengthContext::LengthType::Horizontal),
                            lctx.resolve(fCy, SVGLengthContext::LengthType::Vertical));

  // TODO(YGAurora): MakeTwoPointConical are unimplemented in tgfx
  if (radius == 0) {
    const auto lastColor = colors.size() > 0 ? *colors.end() : Color::Black();
    return Shader::MakeColorShader(lastColor);
  }
  matrix.mapPoints(&center, 1);
  radius *= matrix.getAxisScales().x;
  return Shader::MakeRadialGradient(center, radius, colors, position);
}
#endif
}  // namespace tgfx