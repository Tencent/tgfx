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
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/node/SVGFilterContext.h"

namespace tgfx {

std::shared_ptr<ImageFilter> SVGFe::makeImageFilter(const SVGRenderContext& ctx,
                                                    const SVGFilterContext& filterContext) const {
  return this->onMakeImageFilter(ctx, filterContext);
}

Rect SVGFe::resolveBoundaries(const SVGRenderContext& ctx,
                              const SVGFilterContext& filterContext) const {
  const auto x = X.has_value() ? *X : SVGLength(0, SVGLength::Unit::Percentage);
  const auto y = Y.has_value() ? *Y : SVGLength(0, SVGLength::Unit::Percentage);
  const auto w = Width.has_value() ? *Width : SVGLength(100, SVGLength::Unit::Percentage);
  const auto h = Height.has_value() ? *Height : SVGLength(100, SVGLength::Unit::Percentage);

  return ctx.resolveOBBRect(x, y, w, h, filterContext.primitiveUnits());
}

static bool AnyIsStandardInput(const SVGFilterContext& fctx,
                               const std::vector<SVGFeInputType>& inputs) {
  for (const auto& in : inputs) {
    switch (in.type()) {
      case SVGFeInputType::Type::FilterPrimitiveReference:
        break;
      case SVGFeInputType::Type::SourceGraphic:
      case SVGFeInputType::Type::SourceAlpha:
      case SVGFeInputType::Type::BackgroundImage:
      case SVGFeInputType::Type::BackgroundAlpha:
      case SVGFeInputType::Type::FillPaint:
      case SVGFeInputType::Type::StrokePaint:
        return true;
      case SVGFeInputType::Type::Unspecified:
        // Unspecified means previous result (which may be SourceGraphic).
        if (fctx.previousResultIsSourceGraphic()) {
          return true;
        }
        break;
    }
  }

  return false;
}

Rect SVGFe::resolveFilterSubregion(const SVGRenderContext& ctx,
                                   const SVGFilterContext& fctx) const {
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
  return Rect::MakeXYWH(X.has_value() ? boundaries.left : defaultSubregion.left,
                        Y.has_value() ? boundaries.top : defaultSubregion.top,
                        Width.has_value() ? boundaries.width() : defaultSubregion.width(),
                        Height.has_value() ? boundaries.height() : defaultSubregion.height());
}

SVGColorspace SVGFe::resolveColorspace(const SVGRenderContext& ctx, const SVGFilterContext&) const {
  constexpr SVGColorspace kDefaultCS = SVGColorspace::SRGB;
  const SVGColorspace cs = *ctx.presentationContext()._inherited.ColorInterpolationFilters;
  return cs == SVGColorspace::Auto ? kDefaultCS : cs;
}

void SVGFe::applyProperties(SVGRenderContext* ctx) const {
  this->onPrepareToRender(ctx);
}

bool SVGFe::parseAndSetAttribute(const std::string& name, const std::string& value) {
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
      {"SourceGraphic", SVGFeInputType::Type::SourceGraphic},
      {"SourceAlpha", SVGFeInputType::Type::SourceAlpha},
      {"BackgroundImage", SVGFeInputType::Type::BackgroundImage},
      {"BackgroundAlpha", SVGFeInputType::Type::BackgroundAlpha},
      {"FillPaint", SVGFeInputType::Type::FillPaint},
      {"StrokePaint", SVGFeInputType::Type::StrokePaint},
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