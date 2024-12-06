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

#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Recorder.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

bool SkSVGMask::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setMaskUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("maskUnits", n, v)) ||
         this->setMaskContentUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("maskContentUnits", n, v));
}

#ifndef RENDER_SVG
Rect SkSVGMask::bounds(const SVGRenderContext& context) const {
  auto lengthContext = context.lengthContext();
  lengthContext.setPatternUnits(fMaskUnits);
  SVGRenderContext resolveContext(context, lengthContext);
  if (fWidth.has_value() && fHeight.has_value()) {
    return resolveContext.resolveOBBRect(fX.value_or(SVGLength(0, SVGLength::Unit::Number)),
                                         fY.value_or(SVGLength(0, SVGLength::Unit::Number)),
                                         fWidth.value(), fHeight.value(), fMaskUnits);
  }
  return Rect::MakeEmpty();
}

/** See ITU-R Recommendation BT.709 at http://www.itu.int/rec/R-REC-BT.709/ .*/
constexpr float LUM_COEFF_R = 0.2126f;
constexpr float LUM_COEFF_G = 0.7152f;
constexpr float LUM_COEFF_B = 0.0722f;

std::array<float, 20> MakeLuminanceToAlpha() {
  return std::array<float, 20>{0, 0, 0, 0, 0, 0,           0,           0,           0, 0,
                               0, 0, 0, 0, 0, LUM_COEFF_R, LUM_COEFF_G, LUM_COEFF_B, 0, 0};
}

void SkSVGMask::renderMask(const SVGRenderContext& context) const {
  // https://www.w3.org/TR/SVG11/masking.html#Masking
  // Propagate any inherited properties that may impact mask effect behavior (e.g.
  // color-interpolation). We call this explicitly here because the SkSVGMask
  // nodes do not participate in the normal onRender path, which is when property
  // propagation currently occurs.
  // The local context also restores the filter layer created below on scope exit.

  Recorder recorder;
  auto* canvas = recorder.beginRecording();
  {
    // Ensure the render context is destructed, drawing to the canvas upon destruction
    SVGRenderContext localContext(context, canvas);
    this->onPrepareToRender(&localContext);
    for (const auto& child : fChildren) {
      child->render(localContext);
    }
  }
  auto picture = recorder.finishRecordingAsPicture();
  if (!picture) {
    return;
  }
  auto luminanceFilter = ColorFilter::Matrix(MakeLuminanceToAlpha());
  Paint luminancePaint;
  luminancePaint.setColorFilter(luminanceFilter);
  context.canvas()->drawPicture(picture, nullptr, &luminancePaint);
}
#endif
}  // namespace tgfx