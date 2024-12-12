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

#include "tgfx/svg/node/SVGPoly.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

SVGPoly::SVGPoly(SVGTag t) : INHERITED(t) {
}

bool SVGPoly::parseAndSetAttribute(const std::string& n, const std::string& v) {
  if (INHERITED::parseAndSetAttribute(n, v)) {
    return true;
  }

  if (this->setPoints(SVGAttributeParser::parse<SVGPointsType>("points", n, v))) {
    // TODO: we can likely just keep the points array and create the SkPath when needed.
    // fPath = SkPath::Polygon(
    //         fPoints.data(), fPoints.size(),
    //         this->tag() == SkSVGTag::kPolygon);  // only polygons are auto-closed
  }

  // No other attributes on this node
  return false;
}

void SVGPoly::onDraw(Canvas* canvas, const SVGLengthContext&, const Paint& paint,
                     PathFillType fillType) const {
  // the passed fillType follows inheritance rules and needs to be applied at draw time.
  fPath.setFillType(fillType);
  canvas->drawPath(fPath, paint);
}

Path SVGPoly::onAsPath(const SVGRenderContext& ctx) const {
  Path path = fPath;

  // clip-rule can be inherited and needs to be applied at clip time.
  path.setFillType(ctx.presentationContext()._inherited.ClipRule->asFillType());

  this->mapToParent(&path);
  return path;
}

Rect SVGPoly::onObjectBoundingBox(const SVGRenderContext&) const {
  return fPath.getBounds();
}

}  // namespace tgfx