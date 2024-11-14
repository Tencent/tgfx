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
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGStop.h"

namespace tgfx {

class SkSVGGradient : public SVGHiddenContainer {
 public:
  SVG_ATTR(Href, SVGIRI, SVGIRI())
  SVG_ATTR(GradientTransform, SVGTransformType, SVGTransformType(Matrix::I()))
  SVG_ATTR(SpreadMethod, SVGSpreadMethod, SVGSpreadMethod(SVGSpreadMethod::Type::kPad))
  SVG_ATTR(GradientUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox))

 protected:
  explicit SkSVGGradient(SVGTag t) : INHERITED(t) {
  }

  bool parseAndSetAttribute(const char*, const char*) override;

  bool onAsPaint(const SVGRenderContext&, Paint*) const final;

  virtual std::shared_ptr<Shader> onMakeShader(const SVGRenderContext&, const std::vector<Color>&,
                                               const std::vector<float>&, TileMode,
                                               const Matrix& localMatrix) const = 0;

 private:
  void collectColorStops(const SVGRenderContext&, std::vector<Color>&, std::vector<float>&) const;
  Color resolveStopColor(const SVGRenderContext&, const SkSVGStop&) const;

  using INHERITED = SVGHiddenContainer;
};
}  // namespace tgfx
