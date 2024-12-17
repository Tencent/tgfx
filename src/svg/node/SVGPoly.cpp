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
#include "tgfx/core/Path.h"

namespace tgfx {

SVGPoly::SVGPoly(SVGTag t) : INHERITED(t) {
}

bool SVGPoly::parseAndSetAttribute(const std::string& n, const std::string& v) {
  if (INHERITED::parseAndSetAttribute(n, v)) {
    return true;
  }

  if (this->setPoints(SVGAttributeParser::parse<SVGPointsType>("points", n, v))) {
    // TODO (YGAurora): construct the polygon path by points.
  }
  return false;
}

void SVGPoly::onDraw(Canvas* canvas, const SVGLengthContext&, const Paint& paint,
                     PathFillType fillType) const {
  // the passed fillType follows inheritance rules and needs to be applied at draw time.
  path.setFillType(fillType);
  canvas->drawPath(path, paint);
}

Path SVGPoly::onAsPath(const SVGRenderContext& context) const {
  Path resultPath = path;

  // clip-rule can be inherited and needs to be applied at clip time.
  resultPath.setFillType(context.presentationContext()._inherited.ClipRule->asFillType());

  this->mapToParent(&resultPath);
  return resultPath;
}

Rect SVGPoly::onObjectBoundingBox(const SVGRenderContext&) const {
  return path.getBounds();
}

}  // namespace tgfx