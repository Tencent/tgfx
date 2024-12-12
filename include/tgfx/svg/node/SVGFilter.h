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
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/node/SVGHiddenContainer.h"
#include "tgfx/svg/node/SVGNode.h"

namespace tgfx {

class SVGFilter final : public SVGHiddenContainer {
 public:
  static std::shared_ptr<SVGFilter> Make() {
    return std::shared_ptr<SVGFilter>(new SVGFilter());
  }

  /** Propagates any inherited presentation attributes in the given context. */
  void applyProperties(SVGRenderContext*) const;

  std::shared_ptr<ImageFilter> buildFilterDAG(const SVGRenderContext&) const;

  SVG_ATTR(X, SVGLength, SVGLength(-10, SVGLength::Unit::Percentage))
  SVG_ATTR(Y, SVGLength, SVGLength(-10, SVGLength::Unit::Percentage))
  SVG_ATTR(Width, SVGLength, SVGLength(120, SVGLength::Unit::Percentage))
  SVG_ATTR(Height, SVGLength, SVGLength(120, SVGLength::Unit::Percentage))
  SVG_ATTR(FilterUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox))
  SVG_ATTR(PrimitiveUnits, SVGObjectBoundingBoxUnits,
           SVGObjectBoundingBoxUnits(SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse))

 private:
  SVGFilter() : INHERITED(SVGTag::Filter) {
  }

  bool parseAndSetAttribute(const std::string&, const std::string&) override;

  using INHERITED = SVGHiddenContainer;
};
}  // namespace tgfx
