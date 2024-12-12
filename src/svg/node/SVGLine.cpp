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

#include "tgfx/svg/node/SVGLine.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

SkSVGLine::SkSVGLine() : INHERITED(SVGTag::Line) {
}

bool SkSVGLine::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX1(SVGAttributeParser::parse<SVGLength>("x1", n, v)) ||
         this->setY1(SVGAttributeParser::parse<SVGLength>("y1", n, v)) ||
         this->setX2(SVGAttributeParser::parse<SVGLength>("x2", n, v)) ||
         this->setY2(SVGAttributeParser::parse<SVGLength>("y2", n, v));
}

#ifndef RENDER_SVG
std::tuple<Point, Point> SkSVGLine::resolve(const SVGLengthContext& lctx) const {
  return std::make_tuple(Point::Make(lctx.resolve(X1, SVGLengthContext::LengthType::Horizontal),
                                     lctx.resolve(Y1, SVGLengthContext::LengthType::Vertical)),
                         Point::Make(lctx.resolve(X2, SVGLengthContext::LengthType::Horizontal),
                                     lctx.resolve(Y2, SVGLengthContext::LengthType::Vertical)));
}

void SkSVGLine::onDraw(Canvas* canvas, const SVGLengthContext& lctx, const Paint& paint,
                       PathFillType) const {
  auto [p0, p1] = this->resolve(lctx);
  canvas->drawLine(p0, p1, paint);
}

Path SkSVGLine::onAsPath(const SVGRenderContext& ctx) const {
  auto [p0, p1] = this->resolve(ctx.lengthContext());

  //TODO (YG) path add methods to support line
  Path path;
  path.moveTo(p0);
  path.lineTo(p1);
  this->mapToParent(&path);

  return path;
}
#endif
}  // namespace tgfx