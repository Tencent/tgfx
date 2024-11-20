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
#include <optional>
#include "tgfx/core/Paint.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SkSVGPattern final : public SVGHiddenContainer {
 public:
  static std::shared_ptr<SkSVGPattern> Make() {
    return std::shared_ptr<SkSVGPattern>(new SkSVGPattern());
  }

  SVG_ATTR(Href, SVGIRI, SVGIRI())
  SVG_OPTIONAL_ATTR(X, SVGLength)
  SVG_OPTIONAL_ATTR(Y, SVGLength)
  SVG_OPTIONAL_ATTR(Width, SVGLength)
  SVG_OPTIONAL_ATTR(Height, SVGLength)
  SVG_OPTIONAL_ATTR(PatternTransform, SVGTransformType)
  SVG_ATTR(PatternUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox))
  SVG_ATTR(ContentUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::kUserSpaceOnUse))

 protected:
  SkSVGPattern();

  bool parseAndSetAttribute(const char*, const char*) override;

#ifndef RENDER_SVG
  bool onAsPaint(const SVGRenderContext&, Paint*) const override;
#endif

 private:
  struct PatternAttributes {
    std::optional<SVGLength> fX, fY, fWidth, fHeight;
    std::optional<SVGTransformType> fPatternTransform;
  };

#ifndef RENDER_SVG
  const SkSVGPattern* resolveHref(const SVGRenderContext&, PatternAttributes*) const;
  const SkSVGPattern* hrefTarget(const SVGRenderContext&) const;
#endif

  using INHERITED = SVGHiddenContainer;
};

}  // namespace tgfx
