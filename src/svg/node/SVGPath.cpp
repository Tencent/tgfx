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

#include "tgfx/svg/node/SVGPath.h"
#include <memory>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGParse.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

SVGPath::SVGPath() : INHERITED(SVGTag::Path) {
}

bool SVGPath::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setShapePath(SVGAttributeParser::parse<Path>("d", n, v));
}

template <>
bool SVGAttributeParser::parse<Path>(Path* path) {
  auto [success, parsePath] = PathParse::FromSVGString(currentPos);
  if (success) {
    *path = *parsePath;
  }
  return success;
}

void SVGPath::onDraw(Canvas* canvas, const SVGLengthContext&, const Paint& paint,
                     PathFillType fillType) const {
  // the passed fillType follows inheritance rules and needs to be applied at draw time.
  Path path = ShapePath;  // Note: point and verb data are CoW
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
}

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