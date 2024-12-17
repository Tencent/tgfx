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
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGFe.h"

namespace tgfx {

bool SVGFilter::parseAndSetAttribute(const std::string& name, const std::string& value) {
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

void SVGFilter::applyProperties(SVGRenderContext* context) const {
  this->onPrepareToRender(context);
}

std::shared_ptr<ImageFilter> SVGFilter::buildFilterDAG(const SVGRenderContext& context) const {
  std::shared_ptr<ImageFilter> filter;
  SVGFilterContext filterContext(context.resolveOBBRect(X, Y, Width, Height, FilterUnits),
                                 PrimitiveUnits);
  SVGRenderContext localContext(context);
  this->applyProperties(&localContext);
  SVGColorspace cs = SVGColorspace::SRGB;
  for (const auto& child : children) {
    if (!SVGFe::IsFilterEffect(child)) {
      continue;
    }

    const auto& feNode = static_cast<const SVGFe&>(*child);
    const auto& feResultType = feNode.getResult();

    // Propagate any inherited properties that may impact filter effect behavior (e.g.
    // color-interpolation-filters). We call this explicitly here because the SVGFe
    // nodes do not participate in the normal onRender path, which is when property
    // propagation currently occurs.
    SVGRenderContext localChildCtx(localContext);
    feNode.applyProperties(&localChildCtx);

    const Rect filterSubregion = feNode.resolveFilterSubregion(localChildCtx, filterContext);
    cs = feNode.resolveColorspace(localChildCtx, filterContext);
    filter = feNode.makeImageFilter(localChildCtx, filterContext);

    if (!feResultType.empty()) {
      filterContext.registerResult(feResultType, filter, filterSubregion, cs);
    }

    // Unspecified 'in' and 'in2' inputs implicitly resolve to the previous filter's result.
    filterContext.setPreviousResult(filter, filterSubregion, cs);
  }

  // Convert to final destination colorspace
  if (cs != SVGColorspace::SRGB) {
    //TODO (YGAurora): Implement color space conversion
    filter = nullptr;
  }

  return filter;
}

}  // namespace tgfx