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

#include "tgfx/svg/node/SVGMask.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

bool SVGMask::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setMaskUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("maskUnits", n, v)) ||
         this->setMaskContentUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("maskContentUnits", n, v)) ||
         this->setMaskType(SVGAttributeParser::parse<SVGMaskType>("mask-type", n, v));
}

Rect SVGMask::bounds(const SVGRenderContext& context) const {
  auto lengthContext = context.lengthContext();
  lengthContext.setBoundingBoxUnits(MaskUnits);
  SVGRenderContext resolveContext(context, lengthContext);
  if (Width.has_value() && Height.has_value()) {
    return resolveContext.resolveOBBRect(X.value_or(SVGLength(0, SVGLength::Unit::Number)),
                                         Y.value_or(SVGLength(0, SVGLength::Unit::Number)), *Width,
                                         *Height, MaskUnits);
  }
  return {};
}

void SVGMask::renderMask(const SVGRenderContext& context) const {
  // https://www.w3.org/TR/SVG11/masking.html#Masking
  // Propagate any inherited properties that may impact mask effect behavior (e.g.
  // color-interpolation). We call this explicitly here because the SkSVGMask
  // nodes do not participate in the normal onRender path, which is when property
  // propagation currently occurs.
  // The local context also restores the filter layer created below on scope exit.

  int saveCount = context.canvas()->getSaveCount();
  if (MaskType.type() != SVGMaskType::Type::Alpha) {
    auto luminanceFilter = ColorFilter::Luma();
    Paint luminancePaint;
    luminancePaint.setColorFilter(luminanceFilter);
    context.canvas()->saveLayer(&luminancePaint);
  }

  {
    SVGRenderContext localContext(context);
    this->onPrepareToRender(&localContext);
    for (const auto& child : children) {
      child->render(localContext);
    }
  }
  context.canvas()->restoreToCount(saveCount);
}

}  // namespace tgfx
