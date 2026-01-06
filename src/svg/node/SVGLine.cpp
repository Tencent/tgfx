/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/svg/node/SVGLine.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"

namespace tgfx {

SVGLine::SVGLine() : INHERITED(SVGTag::Line) {
}

bool SVGLine::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX1(SVGAttributeParser::parse<SVGLength>("x1", n, v)) ||
         this->setY1(SVGAttributeParser::parse<SVGLength>("y1", n, v)) ||
         this->setX2(SVGAttributeParser::parse<SVGLength>("x2", n, v)) ||
         this->setY2(SVGAttributeParser::parse<SVGLength>("y2", n, v));
}

std::tuple<Point, Point> SVGLine::resolve(const SVGLengthContext& lengthContext) const {
  return std::make_tuple(
      Point::Make(lengthContext.resolve(X1, SVGLengthContext::LengthType::Horizontal),
                  lengthContext.resolve(Y1, SVGLengthContext::LengthType::Vertical)),
      Point::Make(lengthContext.resolve(X2, SVGLengthContext::LengthType::Horizontal),
                  lengthContext.resolve(Y2, SVGLengthContext::LengthType::Vertical)));
}

void SVGLine::onDrawFill(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
                         PathFillType) const {
  auto [p0, p1] = this->resolve(lengthContext);
  canvas->drawLine(p0, p1, paint);
}

void SVGLine::onDrawStroke(Canvas* canvas, const SVGLengthContext& lengthContext,
                           const Paint& paint, PathFillType /*fillType*/,
                           std::shared_ptr<PathEffect> pathEffect) const {
  if (!pathEffect) {
    return;
  }

  auto [p0, p1] = this->resolve(lengthContext);

  Path path;
  path.moveTo(p0);
  path.lineTo(p1);
  if (pathEffect->filterPath(&path)) {
    canvas->drawPath(path, paint);
  }
};

Path SVGLine::onAsPath(const SVGRenderContext& context) const {
  auto [p0, p1] = this->resolve(context.lengthContext());

  //TODO (YG) path add methods to support line
  Path path;
  path.moveTo(p0);
  path.lineTo(p1);
  this->mapToParent(&path);

  return path;
}

}  // namespace tgfx
