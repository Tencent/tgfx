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

#include "tgfx/svg/node/SVGRect.h"
#include <optional>
#include "SVGRectPriv.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGRenderContext.h"

namespace tgfx {

SkSVGRect::SkSVGRect() : INHERITED(SVGTag::kRect) {
}

bool SkSVGRect::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setRx(SVGAttributeParser::parse<SVGLength>("rx", n, v)) ||
         this->setRy(SVGAttributeParser::parse<SVGLength>("ry", n, v));
}

#ifndef RENDER_SVG
std::tuple<float, float> ResolveOptionalRadii(const std::optional<SVGLength>& opt_rx,
                                              const std::optional<SVGLength>& opt_ry,
                                              const SVGLengthContext& lctx) {
  // https://www.w3.org/TR/SVG2/shapes.html#RectElement
  //
  // The used values for rx and ry are determined from the computed values by following these
  // steps in order:
  //
  // 1. If both rx and ry have a computed value of auto (since auto is the initial value for both
  //    properties, this will also occur if neither are specified by the author or if all
  //    author-supplied values are invalid), then the used value of both rx and ry is 0.
  //    (This will result in square corners.)
  // 2. Otherwise, convert specified values to absolute values as follows:
  //     1. If rx is set to a length value or a percentage, but ry is auto, calculate an absolute
  //        length equivalent for rx, resolving percentages against the used width of the
  //        rectangle; the absolute value for ry is the same.
  //     2. If ry is set to a length value or a percentage, but rx is auto, calculate the absolute
  //        length equivalent for ry, resolving percentages against the used height of the
  //        rectangle; the absolute value for rx is the same.
  //     3. If both rx and ry were set to lengths or percentages, absolute values are generated
  //        individually, resolving rx percentages against the used width, and resolving ry
  //        percentages against the used height.
  const float rx =
      opt_rx.has_value() ? lctx.resolve(*opt_rx, SVGLengthContext::LengthType::kHorizontal) : 0;
  const float ry =
      opt_ry.has_value() ? lctx.resolve(*opt_ry, SVGLengthContext::LengthType::kVertical) : 0;

  return {opt_rx.has_value() ? rx : ry, opt_ry.has_value() ? ry : rx};
}

RRect SkSVGRect::resolve(const SVGLengthContext& lctx) const {
  const auto rect = lctx.resolveRect(fX, fY, fWidth, fHeight);
  const auto [rx, ry] = ResolveOptionalRadii(fRx, fRy, lctx);

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

// void SkSVGRect::onRender(const SVGRenderContext& ctx) const {
//   const auto fillType = ctx.presentationContext().fInherited.fFillRule->asFillType();

//   auto lengthCtx = ctx.lengthContext();
//   Size rectSize = Size::Make(lengthCtx.resolve(fWidth, SkSVGLengthContext::LengthType::kHorizontal),
//                              lengthCtx.resolve(fHeight, SkSVGLengthContext::LengthType::kVertical));
//   lengthCtx.setViewPort(rectSize);
//   SVGRenderContext paintCtx(ctx, ctx.canvas(), lengthCtx);
//   const auto fillPaint = paintCtx.fillPaint();
//   const auto strokePaint = paintCtx.strokePaint();

//   // TODO: this approach forces duplicate geometry resolution in onDraw(); refactor to avoid.
//   if (fillPaint.has_value()) {
//     this->onDraw(ctx.canvas(), ctx.lengthContext(), fillPaint.value(), fillType);
//   }

//   if (strokePaint.has_value()) {
//     this->onDraw(ctx.canvas(), ctx.lengthContext(), strokePaint.value(), fillType);
//   }
// }

void SkSVGRect::onDraw(Canvas* canvas, const SVGLengthContext& lctx, const Paint& paint,
                       PathFillType) const {
  auto rect = this->resolve(lctx);
  auto offset = Point::Make(rect.rect.left, rect.rect.top);
  rect.rect = rect.rect.makeOffset(-offset.x, -offset.y);
  canvas->save();
  canvas->translate(offset.x, offset.y);
  canvas->drawRRect(rect, paint);
  canvas->restore();
}

Path SkSVGRect::onAsPath(const SVGRenderContext& ctx) const {
  Path path;
  path.addRRect(this->resolve(ctx.lengthContext()));
  this->mapToParent(&path);

  return path;
}

Rect SkSVGRect::onObjectBoundingBox(const SVGRenderContext& ctx) const {
  return ctx.lengthContext().resolveRect(fX, fY, fWidth, fHeight);
}
#endif
}  // namespace tgfx