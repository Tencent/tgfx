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

#include "tgfx/svg/node/SVGRect.h"
#include <optional>
// #include "SVGRectPriv.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

SVGRect::SVGRect() : INHERITED(SVGTag::Rect) {
}

bool SVGRect::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setRx(SVGAttributeParser::parse<SVGLength>("rx", n, v)) ||
         this->setRy(SVGAttributeParser::parse<SVGLength>("ry", n, v));
}

RRect SVGRect::resolve(const SVGLengthContext& lengthContext) const {
  const auto rect = lengthContext.resolveRect(X, Y, Width, Height);
  const auto [rx, ry] = lengthContext.resolveOptionalRadii(Rx, Ry);

  // https://www.w3.org/TR/SVG2/shapes.html#RectElement
  // ...
  // 3. Finally, apply clamping to generate the used values:
  //     1. If the absolute rx (after the above steps) is greater than half of the used width,
  //        then the used value of rx is half of the used width.
  //     2. If the absolute ry (after the above steps) is greater than half of the used height,
  //        then the used value of ry is half of the used height.
  //     3. Otherwise, the used values of rx and ry are the absolute values computed previously.

  RRect rrect;
  rrect.setRectXY(rect, std::min(rx, rect.width() / 2), std::min(ry, rect.height() / 2));
  return rrect;
}

void SVGRect::onDrawFill(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
                         PathFillType) const {
  auto rrect = this->resolve(lengthContext);
  auto offset = Point::Make(rrect.rect.left, rrect.rect.top);
  rrect.rect = rrect.rect.makeOffset(-offset.x, -offset.y);
  canvas->save();
  canvas->translate(offset.x, offset.y);
  canvas->drawRRect(rrect, paint);
  canvas->restore();
}

void SVGRect::onDrawStroke(Canvas* canvas, const SVGLengthContext& lengthContext,
                           const Paint& paint, PathFillType /*fillType*/,
                           std::shared_ptr<PathEffect> pathEffect) const {
  if (!pathEffect) {
    return;
  }

  auto rrect = this->resolve(lengthContext);
  auto offset = Point::Make(rrect.rect.left, rrect.rect.top);
  rrect.rect = rrect.rect.makeOffset(-offset.x, -offset.y);
  Path path;
  path.addRRect(rrect);
  if (pathEffect->filterPath(&path)) {
    canvas->save();
    canvas->translate(offset.x, offset.y);
    canvas->drawPath(path, paint);
    canvas->restore();
  }
};

Path SVGRect::onAsPath(const SVGRenderContext& context) const {
  Path path;
  path.addRRect(this->resolve(context.lengthContext()));
  this->mapToParent(&path);

  return path;
}

Rect SVGRect::onObjectBoundingBox(const SVGRenderContext& context) const {
  return context.lengthContext().resolveRect(X, Y, Width, Height);
}

}  // namespace tgfx
