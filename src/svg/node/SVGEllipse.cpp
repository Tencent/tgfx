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
#include "tgfx/svg/node/SVGEllipse.h"
// #include "SVGRectPriv.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

SVGEllipse::SVGEllipse() : INHERITED(SVGTag::Ellipse) {
}

bool SVGEllipse::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setCx(SVGAttributeParser::parse<SVGLength>("cx", n, v)) ||
         this->setCy(SVGAttributeParser::parse<SVGLength>("cy", n, v)) ||
         this->setRx(SVGAttributeParser::parse<SVGLength>("rx", n, v)) ||
         this->setRy(SVGAttributeParser::parse<SVGLength>("ry", n, v));
}

Rect SVGEllipse::resolve(const SVGLengthContext& lengthContext) const {
  const auto cx = lengthContext.resolve(Cx, SVGLengthContext::LengthType::Horizontal);
  const auto cy = lengthContext.resolve(Cy, SVGLengthContext::LengthType::Vertical);

  // https://www.w3.org/TR/SVG2/shapes.html#EllipseElement
  //
  // An auto value for either rx or ry is converted to a used value, following the rules given
  // above for rectangles (but without any clamping based on width or height).
  const auto [rx, ry] = lengthContext.resolveOptionalRadii(Rx, Ry);

  // A computed value of zero for either dimension, or a computed value of auto for both
  // dimensions, disables rendering of the element.
  return (rx > 0 && ry > 0) ? Rect::MakeXYWH(cx - rx, cy - ry, rx * 2, ry * 2) : Rect::MakeEmpty();
}

void SVGEllipse::onDraw(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
                        PathFillType) const {
  canvas->drawOval(this->resolve(lengthContext), paint);
}

Path SVGEllipse::onAsPath(const SVGRenderContext& context) const {
  Path path;
  path.addOval(this->resolve(context.lengthContext()));
  this->mapToParent(&path);
  return path;
}

}  // namespace tgfx