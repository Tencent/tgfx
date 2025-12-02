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

#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/SVGValue.h"

namespace tgfx {
enum class SVGAttribute {
  ClipRule,
  Color,
  ColorInterpolation,
  ColorInterpolationFilters,
  Cx,     // <circle>, <ellipse>, <radialGradient>: center x position
  Cy,     // <circle>, <ellipse>, <radialGradient>: center y position
  Class,  // css style class iri
  Fill,
  FillOpacity,
  FillRule,
  Filter,
  FilterUnits,
  FontFamily,
  FontSize,
  FontStyle,
  FontWeight,
  Fx,  // <radialGradient>: focal point x position
  Fy,  // <radialGradient>: focal point y position
  GradientUnits,
  GradientTransform,
  Height,
  Href,
  Opacity,
  Points,
  PreserveAspectRatio,
  R,   // <circle>, <radialGradient>: radius
  Rx,  // <ellipse>,<rect>: horizontal (corner) radius
  Ry,  // <ellipse>,<rect>: vertical (corner) radius
  SpreadMethod,
  Stroke,
  StrokeDashArray,
  StrokeDashOffset,
  StrokeOpacity,
  StrokeLineCap,
  StrokeLineJoin,
  StrokeMiterLimit,
  StrokeWidth,
  Transform,
  Text,
  TextAnchor,
  ViewBox,
  Visibility,
  Width,
  X,
  X1,  // <line>: first endpoint x
  X2,  // <line>: second endpoint x
  Y,
  Y1,  // <line>: first endpoint y
  Y2,  // <line>: second endpoint y

  Unknown,
};

struct SVGPresentationAttributes {
  static SVGPresentationAttributes MakeInitial();

  SVGProperty<SVGPaint, true> Fill;
  SVGProperty<SVGNumberType, true> FillOpacity;
  SVGProperty<SVGFillRule, true> FillRule;
  SVGProperty<SVGFillRule, true> ClipRule;

  SVGProperty<SVGPaint, true> Stroke;
  SVGProperty<SVGDashArray, true> StrokeDashArray;
  SVGProperty<SVGLength, true> StrokeDashOffset;
  SVGProperty<SVGLineCap, true> StrokeLineCap;
  SVGProperty<SVGLineJoin, true> StrokeLineJoin;
  SVGProperty<SVGNumberType, true> StrokeMiterLimit;
  SVGProperty<SVGNumberType, true> StrokeOpacity;
  SVGProperty<SVGLength, true> StrokeWidth;

  SVGProperty<SVGVisibility, true> Visibility;

  SVGProperty<SVGColorType, true> Color;
  SVGProperty<SVGColorspace, true> ColorInterpolation;
  SVGProperty<SVGColorspace, true> ColorInterpolationFilters;

  SVGProperty<SVGFontFamily, true> FontFamily;
  SVGProperty<SVGFontStyle, true> FontStyle;
  SVGProperty<SVGFontSize, true> FontSize;
  SVGProperty<SVGFontWeight, true> FontWeight;
  SVGProperty<SVGTextAnchor, true> TextAnchor;

  // uninherited
  SVGProperty<SVGNumberType, false> Opacity;
  SVGProperty<SVGFuncIRI, false> ClipPath;
  SVGProperty<SVGStringType, false> Class;
  SVGProperty<SVGDisplay, false> Display;
  SVGProperty<SVGFuncIRI, false> Mask;
  SVGProperty<SVGFuncIRI, false> Filter;
  SVGProperty<SVGColor, false> StopColor;
  SVGProperty<SVGNumberType, false> StopOpacity;
  SVGProperty<SVGColor, false> FloodColor;
  SVGProperty<SVGNumberType, false> FloodOpacity;
  SVGProperty<SVGStringType, false> ID;
  SVGProperty<SVGColor, false> LightingColor;
};
}  // namespace tgfx