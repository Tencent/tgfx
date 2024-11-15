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

#include "tgfx/svg/node/SVGCircle.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

SVGCircle::SVGCircle() : INHERITED(SVGTag::kCircle) {
}

bool SVGCircle::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setCx(SVGAttributeParser::parse<SVGLength>("cx", n, v)) ||
         this->setCy(SVGAttributeParser::parse<SVGLength>("cy", n, v)) ||
         this->setR(SVGAttributeParser::parse<SVGLength>("r", n, v));
}

#ifndef RENDER_SVG
std::tuple<Point, float> SVGCircle::resolve(const SVGLengthContext& lctx) const {
  const auto cx = lctx.resolve(fCx, SVGLengthContext::LengthType::Horizontal);
  const auto cy = lctx.resolve(fCy, SVGLengthContext::LengthType::Vertical);
  const auto r = lctx.resolve(fR, SVGLengthContext::LengthType::Other);

  return std::make_tuple(Point::Make(cx, cy), r);
}

void SVGCircle::onDraw(Canvas* canvas, const SVGLengthContext& lctx, const Paint& paint,
                       PathFillType) const {
  auto [pos, r] = this->resolve(lctx);

  if (r > 0) {
    canvas->drawCircle(pos.x, pos.y, r, paint);
  }
}

Path SVGCircle::onAsPath(const SVGRenderContext& ctx) const {
  auto [pos, r] = this->resolve(ctx.lengthContext());

  Path path;
  path.addOval(Rect::MakeXYWH(pos.x - r, pos.y - r, 2 * r, 2 * r));
  this->mapToParent(&path);
  return path;
}

Rect SVGCircle::onObjectBoundingBox(const SVGRenderContext& ctx) const {
  const auto [pos, r] = this->resolve(ctx.lengthContext());
  return Rect::MakeXYWH(pos.x - r, pos.y - r, 2 * r, 2 * r);
}
#endif
}  // namespace tgfx