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

#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/core/Color.h"

namespace tgfx {
SVGPresentationAttributes SVGPresentationAttributes::MakeInitial() {
  SVGPresentationAttributes result;

  result.Fill.set(SVGPaint(SVGColor(Color::Black())));
  result.FillOpacity.set(static_cast<SVGNumberType>(1));
  result.FillRule.set(SVGFillRule(SVGFillRule::Type::NonZero));
  result.ClipRule.set(SVGFillRule(SVGFillRule::Type::NonZero));

  result.Stroke.set(SVGPaint(SVGPaint::Type::None));
  result.StrokeDashArray.set(SVGDashArray(SVGDashArray::Type::None));
  result.StrokeDashOffset.set(SVGLength(0));
  result.StrokeLineCap.set(SVGLineCap::Butt);
  result.StrokeLineJoin.set(SVGLineJoin(SVGLineJoin::Type::Miter));
  result.StrokeMiterLimit.set(static_cast<SVGNumberType>(4));
  result.StrokeOpacity.set(static_cast<SVGNumberType>(1));
  result.StrokeWidth.set(SVGLength(1));

  result.Visibility.set(SVGVisibility(SVGVisibility::Type::Visible));

  result.Color.set(SVGColorType(Color::Black()));
  result.ColorInterpolation.set(SVGColorspace::SRGB);
  result.ColorInterpolationFilters.set(SVGColorspace::LinearRGB);

  result.FontFamily.init("default");
  result.FontStyle.init(SVGFontStyle::Type::Normal);
  result.FontSize.init(SVGLength(24));
  result.FontWeight.init(SVGFontWeight::Type::Normal);
  result.TextAnchor.init(SVGTextAnchor::Type::Start);

  result.Display.init(SVGDisplay::Inline);

  result.StopColor.set(SVGColor(Color::Black()));
  result.StopOpacity.set(static_cast<SVGNumberType>(1));
  result.FloodColor.set(SVGColor(Color::Black()));
  result.FloodOpacity.set(static_cast<SVGNumberType>(1));
  result.LightingColor.set(SVGColor(Color::White()));

  return result;
}

}  // namespace tgfx
