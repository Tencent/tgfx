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

#include "tgfx/layers/Layer.h"
#include "tgfx/layers/TextAlign.h"
#include "tgfx/core/GradientType.h"

namespace tgfx
{
struct LayerStyleCommonAttribute {
  LayerStyleType type;
  BlendMode blendMode;
  LayerStylePosition position;
  LayerStyleExtraSourceType sourceType;
};

struct BackGroundBlurStyleAttribute {
  LayerStyleCommonAttribute commonAttribute;
  float blurrinessX;
  float blurrinessY;
  TileMode tileMode;
};

struct DropShadowStyleAttribute {
  LayerStyleCommonAttribute commonAttribute;
  float offsetX;
  float offsetY;
  float blurrinessX;
  float blurrinessY;
  Color color;
  bool showbehindLayer;
};

struct InnerShadowStyleAttribute {
  LayerStyleCommonAttribute commonAttribute;
  float offsetX;
  float offsetY;
  float blurrinessX;
  float blurrinessY;
  Color color;
};

struct LayerfilterCommonAttribute {
  LayerFilterType type;
};

struct BlendFilterAttribute {
  LayerfilterCommonAttribute commonAttribute;
  Color color;
  BlendMode blendMode;
};

struct BlurFilterAttribute {
  LayerfilterCommonAttribute commonAttribute;
  float blurrinessX;
  float blurrinessY;
  TileMode tileMode;
};

struct ColorFilterAttribute {
  LayerfilterCommonAttribute commonAttribute;
  std::array<float, 20> matrix;
};

struct DropShadowFilterAttribute {
  LayerfilterCommonAttribute commonAttribute;
  float offsetX;
  float offsetY;
  float blurrinessX;
  float blurrinessY;
  Color color;
  bool dropShadowOnly;
};

struct InnerShadowFilterAttribute {
  LayerfilterCommonAttribute commonAttribute;
  float offsetX;
  float offsetY;
  float blurrinessX;
  float blurrinessY;
  Color color;
  bool innerShadowOnly;
};

struct LayerCommonAttribute {
  LayerType type;
  std::string name;
  float alpha;
  BlendMode blendMode;
  Point position;
  bool visible;
  bool rasterize;
  float rasterizeScale;
  bool edgeAntialiasing;
  bool groupOpacity;
  std::vector<BackGroundBlurStyleAttribute> backgroundStyleAttributes;
  std::vector<DropShadowStyleAttribute> dropShadowStyleAttributes;
  std::vector<InnerShadowStyleAttribute> innerShadowStyleAttributes;
  std::vector<BlendFilterAttribute> blendFilterAttributes;
  std::vector<BlurFilterAttribute> blurFilterAttributes;
  std::vector<ColorFilterAttribute> colorFilterAttributes;
  std::vector<DropShadowFilterAttribute> dropShadowFilterAttributes;
  std::vector<InnerShadowFilterAttribute> innerShadowFilterAttributes;
};

struct ImageAttribute {
  int imageType;
  int imageWidth;
  int imageHeight;
  bool imageAlphaOnly;
  bool imageMipmap;
  bool imageFullyDecode;
  bool imageTextureBacked;
};

struct ImageLayerAttribute {
  LayerCommonAttribute commonAttribute;
  FilterMode filterMode;
  MipmapMode mipmapMode;
  ImageAttribute image;
};

struct ShapeStyleCommonAttribute {
  float shapeStyleAlpha;
  BlendMode blendMode;
};

struct ImagePatternAttribute {
  ShapeStyleCommonAttribute commonAttribute;
  TileMode tileModeX;
  TileMode tileModeY;
  FilterMode filterMode;
  MipmapMode mipMapMode;
  ImageAttribute image;
};

struct GradientAttribute {
  ShapeStyleCommonAttribute commonAttribute;
  GradientType type;
  std::vector<Color> colors;
  std::vector<float> positions;
};

struct LinearGradientAttribute {
  GradientAttribute gradientAttribute;
  Point startPoint;
  Point endPoint;
};

struct RadialGradientAttribute {
  GradientAttribute gradientAttribute;
  Point center;
  float radius;
};

struct ConicGradientAttribute {
  GradientAttribute gradientAttribute;
  Point center;
  float startAngle;
  float endAngle;
};

struct DiamondGradientAttribute {
  GradientAttribute gradientAttribute;
  Point center;
  float halfDiagonal;
};

struct ShapeLayerAttribute {
  LayerCommonAttribute commonAttribute;
  PathFillType pathFillType;
  bool pathIsLine;
  bool pathIsRect;
  bool pathIsOval;
  bool pathIsEmpty;
  Rect pathBounds;
  int pathPointCount;
  int pathVerbsCount;
  std::vector<LinearGradientAttribute> linearGradientAttributes;
  std::vector<RadialGradientAttribute> radialGradientAttributes;
  std::vector<ConicGradientAttribute> conicGradientAttributes;
  std::vector<DiamondGradientAttribute> diamondGradientAttributes;
  std::vector<ImagePatternAttribute> imagePatternAttributes;
};

struct SolidLayerAttribute {
  LayerCommonAttribute commonAttribute;
  float width;
  float height;
  float solidRadiusX;
  float solidRadiusY;
  Color solidColor;
};

struct TextLayerAttribute {
  LayerCommonAttribute commonAttribute;
  std::string textString;
  Color textColor;
  Font textFont;
  float textWidth;
  float textHeight;
  TextAlign textAlign;
  bool textAutoWrap;
};
}