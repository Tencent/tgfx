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

#include "tgfx/svg/node/SVGFilter.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGRenderContext.h"
#include "tgfx/svg/node/SVGFe.h"
#include "tgfx/svg/node/SVGFilterContext.h"

namespace tgfx {

bool SkSVGFilter::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", name, value)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", name, value)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", name, value)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", name, value)) ||
         this->setFilterUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("filterUnits", name, value)) ||
         this->setPrimitiveUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("primitiveUnits", name, value));
}

void SkSVGFilter::applyProperties(SVGRenderContext* ctx) const {
  this->onPrepareToRender(ctx);
}

std::shared_ptr<ImageFilter> SkSVGFilter::buildFilterDAG(const SVGRenderContext& ctx) const {
  std::shared_ptr<ImageFilter> filter;
  SkSVGFilterContext fctx(ctx.resolveOBBRect(X, Y, Width, Height, FilterUnits), PrimitiveUnits);
  SVGRenderContext localCtx(ctx);
  this->applyProperties(&localCtx);
  SVGColorspace cs = SVGColorspace::SRGB;
  for (const auto& child : fChildren) {
    if (!SkSVGFe::IsFilterEffect(child)) {
      continue;
    }

    const auto& feNode = static_cast<const SkSVGFe&>(*child);
    const auto& feResultType = feNode.getResult();

    // Propagate any inherited properties that may impact filter effect behavior (e.g.
    // color-interpolation-filters). We call this explicitly here because the SkSVGFe
    // nodes do not participate in the normal onRender path, which is when property
    // propagation currently occurs.
    SVGRenderContext localChildCtx(localCtx);
    feNode.applyProperties(&localChildCtx);

    const Rect filterSubregion = feNode.resolveFilterSubregion(localChildCtx, fctx);
    cs = feNode.resolveColorspace(localChildCtx, fctx);
    filter = feNode.makeImageFilter(localChildCtx, fctx);

    if (!feResultType.empty()) {
      fctx.registerResult(feResultType, filter, filterSubregion, cs);
    }

    // Unspecified 'in' and 'in2' inputs implicitly resolve to the previous filter's result.
    fctx.setPreviousResult(filter, filterSubregion, cs);
  }

  // Convert to final destination colorspace
  if (cs != SVGColorspace::SRGB) {
    // filter = SkImageFilters::ColorFilter(SkColorFilters::LinearToSRGBGamma(), filter);

    //TODO (YG)
    filter = nullptr;
  }

  return filter;
}

}  // namespace tgfx