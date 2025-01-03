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

#pragma once

#include <memory>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGShape.h"

namespace tgfx {

// Handles <polygon> and <polyline> elements.
class SVGPoly final : public SVGShape {
 public:
  static std::shared_ptr<SVGPoly> MakePolygon() {
    return std::shared_ptr<SVGPoly>(new SVGPoly(SVGTag::Polygon));
  }

  static std::shared_ptr<SVGPoly> MakePolyline() {
    return std::shared_ptr<SVGPoly>(new SVGPoly(SVGTag::Polyline));
  }

  SVG_ATTR(Points, SVGPointsType, SVGPointsType())

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  void onDraw(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
              PathFillType fillType) const override;

  Path onAsPath(const SVGRenderContext& context) const override;

  Rect onObjectBoundingBox(const SVGRenderContext& context) const override;

 private:
  SVGPoly(SVGTag tag);

  mutable Path path;  // mutated in onDraw(), to apply inherited fill types.

  using INHERITED = SVGShape;
};
}  // namespace tgfx
