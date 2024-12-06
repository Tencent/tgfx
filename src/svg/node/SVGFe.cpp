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

#include "tgfx/svg/node/SVGFe.h"
#include <cstddef>
#include <tuple>
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGRenderContext.h"
#include "tgfx/svg/node/SVGFilterContext.h"

namespace tgfx {

#ifndef RENDER_SVG

std::shared_ptr<ImageFilter> SkSVGFe::makeImageFilter(
    const SVGRenderContext& ctx, const SkSVGFilterContext& filterContext) const {
  return this->onMakeImageFilter(ctx, filterContext);
}

Rect SkSVGFe::resolveBoundaries(const SVGRenderContext& ctx,
                                const SkSVGFilterContext& filterContext) const {
  const auto x = fX.has_value() ? *fX : SVGLength(0, SVGLength::Unit::Percentage);
  const auto y = fY.has_value() ? *fY : SVGLength(0, SVGLength::Unit::Percentage);
  const auto w = fWidth.has_value() ? *fWidth : SVGLength(100, SVGLength::Unit::Percentage);
  const auto h = fHeight.has_value() ? *fHeight : SVGLength(100, SVGLength::Unit::Percentage);

  return ctx.resolveOBBRect(x, y, w, h, filterContext.primitiveUnits());
}

static bool AnyIsStandardInput(const SkSVGFilterContext& fctx,
                               const std::vector<SVGFeInputType>& inputs) {
  for (const auto& in : inputs) {
    switch (in.type()) {
      case SVGFeInputType::Type::kFilterPrimitiveReference:
        break;
      case SVGFeInputType::Type::kSourceGraphic:
      case SVGFeInputType::Type::kSourceAlpha:
      case SVGFeInputType::Type::kBackgroundImage:
      case SVGFeInputType::Type::kBackgroundAlpha:
      case SVGFeInputType::Type::kFillPaint:
      case SVGFeInputType::Type::kStrokePaint:
        return true;
      case SVGFeInputType::Type::kUnspecified:
        // Unspecified means previous result (which may be SourceGraphic).
        if (fctx.previousResultIsSourceGraphic()) {
          return true;
        }
        break;
    }
  }

  return false;
}

Rect SkSVGFe::resolveFilterSubregion(const SVGRenderContext& ctx,
                                     const SkSVGFilterContext& fctx) const {
  // From https://www.w3.org/TR/SVG11/filters.html#FilterPrimitiveSubRegion,
  // the default filter effect subregion is equal to the union of the subregions defined
  // for all "referenced nodes" (filter effect inputs). If there are no inputs, the
  // default subregion is equal to the filter effects region
  // (https://www.w3.org/TR/SVG11/filters.html#FilterEffectsRegion).
  const std::vector<SVGFeInputType> inputs = this->getInputs();
  Rect defaultSubregion;
  if (inputs.empty() || AnyIsStandardInput(fctx, inputs)) {
    defaultSubregion = fctx.filterEffectsRegion();
  } else {
    defaultSubregion = fctx.filterPrimitiveSubregion(inputs[0]);
    for (size_t i = 1; i < inputs.size(); i++) {
      defaultSubregion.join(fctx.filterPrimitiveSubregion(inputs[i]));
    }
  }

  // Next resolve the rect specified by the x, y, width, height attributes on this filter effect.
  // If those attributes were given, they override the corresponding attribute of the default
  // filter effect subregion calculated above.
  const Rect boundaries = this->resolveBoundaries(ctx, fctx);

  // Compute and return the fully resolved subregion.
  return Rect::MakeXYWH(fX.has_value() ? boundaries.left : defaultSubregion.left,
                        fY.has_value() ? boundaries.top : defaultSubregion.top,
                        fWidth.has_value() ? boundaries.width() : defaultSubregion.width(),
                        fHeight.has_value() ? boundaries.height() : defaultSubregion.height());
}

SVGColorspace SkSVGFe::resolveColorspace(const SVGRenderContext& ctx,
                                         const SkSVGFilterContext&) const {
  constexpr SVGColorspace kDefaultCS = SVGColorspace::kSRGB;
  const SVGColorspace cs = *ctx.presentationContext()._inherited.fColorInterpolationFilters;
  return cs == SVGColorspace::kAuto ? kDefaultCS : cs;
}

void SkSVGFe::applyProperties(SVGRenderContext* ctx) const {
  this->onPrepareToRender(ctx);
}
#endif

bool SkSVGFe::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn(SVGAttributeParser::parse<SVGFeInputType>("in", name, value)) ||
         this->setResult(SVGAttributeParser::parse<SVGStringType>("result", name, value)) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", name, value)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", name, value)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", name, value)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", name, value));
}

template <>
bool SVGAttributeParser::parse(SVGFeInputType* type) {
  static constexpr std::tuple<const char*, SVGFeInputType::Type> gTypeMap[] = {
      {"SourceGraphic", SVGFeInputType::Type::kSourceGraphic},
      {"SourceAlpha", SVGFeInputType::Type::kSourceAlpha},
      {"BackgroundImage", SVGFeInputType::Type::kBackgroundImage},
      {"BackgroundAlpha", SVGFeInputType::Type::kBackgroundAlpha},
      {"FillPaint", SVGFeInputType::Type::kFillPaint},
      {"StrokePaint", SVGFeInputType::Type::kStrokePaint},
  };

  SVGStringType resultId;
  SVGFeInputType::Type t;
  bool parsedValue = false;
  if (this->parseEnumMap(gTypeMap, &t)) {
    *type = SVGFeInputType(t);
    parsedValue = true;
  } else if (parse(&resultId)) {
    *type = SVGFeInputType(resultId);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}
}  // namespace tgfx