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

#include "tgfx/svg/SVGTypes.h"

namespace tgfx {
enum class SVGAttribute {
  kClipRule,
  kColor,
  kColorInterpolation,
  kColorInterpolationFilters,
  kCx,  // <circle>, <ellipse>, <radialGradient>: center x position
  kCy,  // <circle>, <ellipse>, <radialGradient>: center y position
  kFill,
  kFillOpacity,
  kFillRule,
  kFilter,
  kFilterUnits,
  kFontFamily,
  kFontSize,
  kFontStyle,
  kFontWeight,
  kFx,  // <radialGradient>: focal point x position
  kFy,  // <radialGradient>: focal point y position
  kGradientUnits,
  kGradientTransform,
  kHeight,
  kHref,
  kOpacity,
  kPoints,
  kPreserveAspectRatio,
  kR,   // <circle>, <radialGradient>: radius
  kRx,  // <ellipse>,<rect>: horizontal (corner) radius
  kRy,  // <ellipse>,<rect>: vertical (corner) radius
  kSpreadMethod,
  kStroke,
  kStrokeDashArray,
  kStrokeDashOffset,
  kStrokeOpacity,
  kStrokeLineCap,
  kStrokeLineJoin,
  kStrokeMiterLimit,
  kStrokeWidth,
  kTransform,
  kText,
  kTextAnchor,
  kViewBox,
  kVisibility,
  kWidth,
  kX,
  kX1,  // <line>: first endpoint x
  kX2,  // <line>: second endpoint x
  kY,
  kY1,  // <line>: first endpoint y
  kY2,  // <line>: second endpoint y

  kUnknown,
};

struct SVGPresentationAttributes {
  static SVGPresentationAttributes MakeInitial();

  // TODO: SVGProperty adds an extra ptr per attribute; refactor to reduce overhead.

  SVGProperty<SVGPaint, true> fFill;
  SVGProperty<SVGNumberType, true> fFillOpacity;
  SVGProperty<SVGFillRule, true> fFillRule;
  SVGProperty<SVGFillRule, true> fClipRule;

  SVGProperty<SVGPaint, true> fStroke;
  SVGProperty<SVGDashArray, true> fStrokeDashArray;
  SVGProperty<SVGLength, true> fStrokeDashOffset;
  SVGProperty<SVGLineCap, true> fStrokeLineCap;
  SVGProperty<SVGLineJoin, true> fStrokeLineJoin;
  SVGProperty<SVGNumberType, true> fStrokeMiterLimit;
  SVGProperty<SVGNumberType, true> fStrokeOpacity;
  SVGProperty<SVGLength, true> fStrokeWidth;

  SVGProperty<SVGVisibility, true> fVisibility;

  SVGProperty<SVGColorType, true> fColor;
  SVGProperty<SVGColorspace, true> fColorInterpolation;
  SVGProperty<SVGColorspace, true> fColorInterpolationFilters;

  SVGProperty<SVGFontFamily, true> fFontFamily;
  SVGProperty<SVGFontStyle, true> fFontStyle;
  SVGProperty<SVGFontSize, true> fFontSize;
  SVGProperty<SVGFontWeight, true> fFontWeight;
  SVGProperty<SVGTextAnchor, true> fTextAnchor;

  // uninherited
  SVGProperty<SVGNumberType, false> fOpacity;
  SVGProperty<SVGFuncIRI, false> fClipPath;
  SVGProperty<SVGDisplay, false> fDisplay;
  SVGProperty<SVGFuncIRI, false> fMask;
  SVGProperty<SVGFuncIRI, false> fFilter;
  SVGProperty<SVGColor, false> fStopColor;
  SVGProperty<SVGNumberType, false> fStopOpacity;
  SVGProperty<SVGColor, false> fFloodColor;
  SVGProperty<SVGNumberType, false> fFloodOpacity;
  SVGProperty<SVGColor, false> fLightingColor;
};
}  // namespace tgfx