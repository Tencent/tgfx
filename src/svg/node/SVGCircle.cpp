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

#include "tgfx/svg/node/SVGCircle.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

SVGCircle::SVGCircle() : INHERITED(SVGTag::Circle) {
}

bool SVGCircle::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         setCx(SVGAttributeParser::parse<SVGLength>("cx", name, value)) ||
         setCy(SVGAttributeParser::parse<SVGLength>("cy", name, value)) ||
         setR(SVGAttributeParser::parse<SVGLength>("r", name, value));
}

std::tuple<Point, float> SVGCircle::resolve(const SVGLengthContext& lengthContext) const {
  const auto cx = lengthContext.resolve(Cx, SVGLengthContext::LengthType::Horizontal);
  const auto cy = lengthContext.resolve(Cy, SVGLengthContext::LengthType::Vertical);
  const auto r = lengthContext.resolve(R, SVGLengthContext::LengthType::Other);

  return std::make_tuple(Point::Make(cx, cy), r);
}

void SVGCircle::onDrawFill(Canvas* canvas, const SVGLengthContext& lengthContext,
                           const Paint& paint, PathFillType /*type*/) const {
  auto [pos, r] = resolve(lengthContext);

  if (r > 0) {
    canvas->drawCircle(pos.x, pos.y, r, paint);
  }
}

void SVGCircle::onDrawStroke(Canvas* canvas, const SVGLengthContext& lengthContext,
                             const Paint& paint, PathFillType /*fillType*/,
                             std::shared_ptr<PathEffect> pathEffect) const {
  if (!pathEffect) {
    return;
  }

  auto [pos, r] = resolve(lengthContext);
  if (r > 0) {
    Path path;
    path.addOval(Rect::MakeXYWH(pos.x - r, pos.y - r, 2 * r, 2 * r));
    if (pathEffect->filterPath(&path)) {
      canvas->drawPath(path, paint);
    }
  }
};

Path SVGCircle::onAsPath(const SVGRenderContext& context) const {
  auto [pos, r] = this->resolve(context.lengthContext());

  Path path;
  path.addOval(Rect::MakeXYWH(pos.x - r, pos.y - r, 2 * r, 2 * r));
  this->mapToParent(&path);
  return path;
}

Rect SVGCircle::onObjectBoundingBox(const SVGRenderContext& context) const {
  const auto [pos, r] = this->resolve(context.lengthContext());
  return Rect::MakeXYWH(pos.x - r, pos.y - r, 2 * r, 2 * r);
}
}  // namespace tgfx
