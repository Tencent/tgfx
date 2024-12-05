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
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGParse.h"

namespace tgfx {

SkSVGPath::SkSVGPath() : INHERITED(SVGTag::Path) {
}

bool SkSVGPath::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setPath(SVGAttributeParser::parse<Path>("d", n, v));
}

template <>
bool SVGAttributeParser::parse<Path>(Path* path) {
  auto [success, parsePath] = PathParse::FromSVGString(fCurPos);
  if (success) {
    *path = *parsePath;
  }
  return success;
}

#ifndef RENDER_SVG
void SkSVGPath::onDraw(Canvas* canvas, const SVGLengthContext&, const Paint& paint,
                       PathFillType fillType) const {
  // the passed fillType follows inheritance rules and needs to be applied at draw time.
  Path path = fPath;  // Note: point and verb data are CoW
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
}

Path SkSVGPath::onAsPath(const SVGRenderContext& ctx) const {
  Path path = fPath;
  // clip-rule can be inherited and needs to be applied at clip time.
  path.setFillType(ctx.presentationContext()._inherited.fClipRule->asFillType());
  this->mapToParent(&path);
  return path;
}

Rect SkSVGPath::onObjectBoundingBox(const SVGRenderContext&) const {
  return fPath.getBounds();
  //TODO (YG): Implement this
  // return fPath.computeTightBounds();
}
#endif
}  // namespace tgfx