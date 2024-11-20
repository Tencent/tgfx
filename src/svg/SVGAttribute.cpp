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

#include "tgfx/svg/SVGAttribute.h"
#include "tgfx/core/Color.h"

namespace tgfx {
SVGPresentationAttributes SVGPresentationAttributes::MakeInitial() {
  SVGPresentationAttributes result;

  result.fFill.set(SVGPaint(SVGColor(Color::Black())));
  result.fFillOpacity.set(static_cast<SVGNumberType>(1));
  result.fFillRule.set(SVGFillRule(SVGFillRule::Type::kNonZero));
  result.fClipRule.set(SVGFillRule(SVGFillRule::Type::kNonZero));

  result.fStroke.set(SVGPaint(SVGPaint::Type::kNone));
  result.fStrokeDashArray.set(SVGDashArray(SVGDashArray::Type::kNone));
  result.fStrokeDashOffset.set(SVGLength(0));
  result.fStrokeLineCap.set(SVGLineCap::kButt);
  result.fStrokeLineJoin.set(SVGLineJoin(SVGLineJoin::Type::kMiter));
  result.fStrokeMiterLimit.set(static_cast<SVGNumberType>(4));
  result.fStrokeOpacity.set(static_cast<SVGNumberType>(1));
  result.fStrokeWidth.set(SVGLength(1));

  result.fVisibility.set(SVGVisibility(SVGVisibility::Type::kVisible));

  result.fColor.set(SVGColorType(Color::Black()));
  result.fColorInterpolation.set(SVGColorspace::kSRGB);
  result.fColorInterpolationFilters.set(SVGColorspace::kLinearRGB);

  result.fFontFamily.init("default");
  result.fFontStyle.init(SVGFontStyle::Type::kNormal);
  result.fFontSize.init(SVGLength(24));
  result.fFontWeight.init(SVGFontWeight::Type::kNormal);
  result.fTextAnchor.init(SVGTextAnchor::Type::kStart);

  result.fDisplay.init(SVGDisplay::kInline);

  result.fStopColor.set(SVGColor(Color::Black()));
  result.fStopOpacity.set(static_cast<SVGNumberType>(1));
  result.fFloodColor.set(SVGColor(Color::Black()));
  result.fFloodOpacity.set(static_cast<SVGNumberType>(1));
  result.fLightingColor.set(SVGColor(Color::White()));

  return result;
}

}  // namespace tgfx
