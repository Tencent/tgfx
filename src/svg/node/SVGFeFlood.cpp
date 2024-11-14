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

// #include "tgfx/svg/SVGFeFlood.h"

// class SkSVGFilterContext;

#ifdef RENDER_SVG
SkColor SkSVGFeFlood::resolveFloodColor(const SVGRenderContext& ctx) const {
  const auto floodColor = this->getFloodColor();
  const auto floodOpacity = this->getFloodOpacity();
  // Uninherited presentation attributes should have a concrete value by now.
  if (!floodColor.isValue() || !floodOpacity.isValue()) {
    SkDebugf("unhandled: flood-color or flood-opacity has no value\n");
    return SK_ColorBLACK;
  }

  const SkColor color = ctx.resolveSvgColor(*floodColor);
  return SkColorSetA(color, SkScalarRoundToInt(*floodOpacity * 255));
}

sk_sp<SkImageFilter> SkSVGFeFlood::onMakeImageFilter(const SVGRenderContext& ctx,
                                                     const SkSVGFilterContext& fctx) const {
  return SkImageFilters::Shader(SkShaders::Color(resolveFloodColor(ctx)),
                                this->resolveFilterSubregion(ctx, fctx));
}
#endif