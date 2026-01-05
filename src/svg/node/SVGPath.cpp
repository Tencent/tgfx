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

#include "tgfx/svg/node/SVGPath.h"
#include <memory>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "svg/SVGUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGPathParser.h"

namespace tgfx {

SVGPath::SVGPath() : INHERITED(SVGTag::Path) {
}

bool SVGPath::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setShapePath(SVGAttributeParser::parse<Path>("d", n, v));
}

template <>
bool SVGAttributeParser::parse<Path>(Path* path) {
  auto parsePath = SVGPathParser::FromSVGString(currentPos);
  if (parsePath) {
    *path = *parsePath;
    return true;
  }
  return false;
}

void SVGPath::onDrawFill(Canvas* canvas, const SVGLengthContext&, const Paint& paint,
                         PathFillType fillType) const {
  // the passed fillType follows inheritance rules and needs to be applied at draw time.
  Path path = ShapePath;
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
}

void SVGPath::onDrawStroke(Canvas* canvas, const SVGLengthContext& /*lengthContext*/,
                           const Paint& paint, PathFillType fillType,
                           std::shared_ptr<PathEffect> pathEffect) const {
  if (!pathEffect) {
    return;
  }

  Path path = ShapePath;
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
  if (pathEffect->filterPath(&path)) {
    canvas->drawPath(path, paint);
  }
};

Path SVGPath::onAsPath(const SVGRenderContext& context) const {
  Path path = ShapePath;
  // clip-rule can be inherited and needs to be applied at clip time.
  path.setFillType(context.presentationContext()._inherited.ClipRule->asFillType());
  this->mapToParent(&path);
  return path;
}

Rect SVGPath::onObjectBoundingBox(const SVGRenderContext&) const {
  return ShapePath.getBounds();
  //TODO (YGAurora): Implement thigh bounds computation
}
}  // namespace tgfx
