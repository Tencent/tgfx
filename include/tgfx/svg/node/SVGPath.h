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

#pragma once

#include <memory>
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGShape.h"

namespace tgfx {

class SVGPath final : public SVGShape {
 public:
  static std::shared_ptr<SVGPath> Make() {
    return std::shared_ptr<SVGPath>(new SVGPath());
  }

  SVG_ATTR(ShapePath, Path, Path())

 protected:
  bool parseAndSetAttribute(const std::string& name, const std::string& value) override;

  void onDrawFill(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
                  PathFillType fillType) const override;

  void onDrawStroke(Canvas* canvas, const SVGLengthContext& lengthContext, const Paint& paint,
                    PathFillType fillType, std::shared_ptr<PathEffect> pathEffect) const override;

  Path onAsPath(const SVGRenderContext& context) const override;

  Rect onObjectBoundingBox(const SVGRenderContext& context) const override;

 private:
  SVGPath();

  using INHERITED = SVGShape;
};
}  // namespace tgfx
