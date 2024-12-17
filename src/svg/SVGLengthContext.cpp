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

#include "SVGLengthContext.h"
#include "core/utils/Log.h"

namespace tgfx {

namespace {

float length_size_for_type(const Size& viewport, SVGLengthContext::LengthType t) {
  switch (t) {
    case SVGLengthContext::LengthType::Horizontal:
      return viewport.width;
    case SVGLengthContext::LengthType::Vertical:
      return viewport.height;
    case SVGLengthContext::LengthType::Other: {
      // https://www.w3.org/TR/SVG11/coords.html#Units_viewport_percentage
      const float rsqrt2 = 1.0f / std::sqrt(2.0f);
      const float w = viewport.width;
      const float h = viewport.height;
      return rsqrt2 * std::sqrt(w * w + h * h);
    }
  }
  ASSERT(false);  // Not reached.
  return 0;
}

// Multipliers for DPI-relative units.
constexpr float INMultiplier = 1.00f;
constexpr float PTMultiplier = INMultiplier / 72.272f;
constexpr float PCMultiplier = PTMultiplier * 12;
constexpr float MMMultiplier = INMultiplier / 25.4f;
constexpr float CMMultiplier = MMMultiplier * 10;

}  // namespace

float SVGLengthContext::resolve(const SVGLength& l, LengthType t) const {
  switch (l.unit()) {
    case SVGLength::Unit::Number: {
      if (patternUnit.has_value()) {
        if (patternUnit.value().type() == SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox) {
          return l.value() * length_size_for_type(_viewPort, t);
        } else {
          return l.value();
        }
      } else {
        return l.value();
      }
    }
    case SVGLength::Unit::PX:
      return l.value();
    case SVGLength::Unit::Percentage:
      return l.value() * length_size_for_type(_viewPort, t) / 100;
    case SVGLength::Unit::CM:
      return l.value() * dpi * CMMultiplier;
    case SVGLength::Unit::MM:
      return l.value() * dpi * MMMultiplier;
    case SVGLength::Unit::IN:
      return l.value() * dpi * INMultiplier;
    case SVGLength::Unit::PT:
      return l.value() * dpi * PTMultiplier;
    case SVGLength::Unit::PC:
      return l.value() * dpi * PCMultiplier;
    default:
      //unsupported unit type
      ASSERT(false);
      return 0;
  }
}

Rect SVGLengthContext::resolveRect(const SVGLength& x, const SVGLength& y, const SVGLength& width,
                                   const SVGLength& height) const {
  return Rect::MakeXYWH(this->resolve(x, SVGLengthContext::LengthType::Horizontal),
                        this->resolve(y, SVGLengthContext::LengthType::Vertical),
                        this->resolve(width, SVGLengthContext::LengthType::Horizontal),
                        this->resolve(height, SVGLengthContext::LengthType::Vertical));
}

std::tuple<float, float> SVGLengthContext::resolveOptionalRadii(
    const std::optional<SVGLength>& optionalRx, const std::optional<SVGLength>& optionalRy) const {
  // https://www.w3.org/TR/SVG2/shapes.html#RectElement
  //
  // The used values for rx and ry are determined from the computed values by following these
  // steps in order:
  //
  // 1. If both rx and ry have a computed value of auto (since auto is the initial value for both
  //    properties, this will also occur if neither are specified by the author or if all
  //    author-supplied values are invalid), then the used value of both rx and ry is 0.
  //    (This will result in square corners.)
  // 2. Otherwise, convert specified values to absolute values as follows:
  //     1. If rx is set to a length value or a percentage, but ry is auto, calculate an absolute
  //        length equivalent for rx, resolving percentages against the used width of the
  //        rectangle; the absolute value for ry is the same.
  //     2. If ry is set to a length value or a percentage, but rx is auto, calculate the absolute
  //        length equivalent for ry, resolving percentages against the used height of the
  //        rectangle; the absolute value for rx is the same.
  //     3. If both rx and ry were set to lengths or percentages, absolute values are generated
  //        individually, resolving rx percentages against the used width, and resolving ry
  //        percentages against the used height.
  const float rx =
      optionalRx.has_value() ? resolve(*optionalRx, SVGLengthContext::LengthType::Horizontal) : 0;
  const float ry =
      optionalRy.has_value() ? resolve(*optionalRy, SVGLengthContext::LengthType::Vertical) : 0;

  return {optionalRx.has_value() ? rx : ry, optionalRy.has_value() ? ry : rx};
}

}  // namespace tgfx