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

#include "tgfx/svg/node/SVGFeMorphology.h"
#include <tuple>
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGFeMorphology::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setMorphOperator(
             SVGAttributeParser::parse<SkSVGFeMorphology::Operator>("operator", name, value)) ||
         this->setMorphRadius(
             SVGAttributeParser::parse<SkSVGFeMorphology::Radius>("radius", name, value));
}

#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> SkSVGFeMorphology::onMakeImageFilter(
    const SVGRenderContext& /*ctx*/, const SkSVGFilterContext& /*fctx*/) const {
  // const SkRect cropRect = this->resolveFilterSubregion(ctx, fctx);
  // const SkSVGColorspace colorspace = this->resolveColorspace(ctx, fctx);
  // sk_sp<SkImageFilter> input = fctx.resolveInput(ctx, this->getIn(), colorspace);

  // const auto r =
  //     SkV2{fRadius.fX, fRadius.fY} * ctx.transformForCurrentOBB(fctx.primitiveUnits()).scale;
  // switch (fOperator) {
  //   case Operator::kErode:
  //     return SkImageFilters::Erode(r.x, r.y, input, cropRect);
  //   case Operator::kDilate:
  //     return SkImageFilters::Dilate(r.x, r.y, input, cropRect);
  // }
  return nullptr;
}
#endif

template <>
bool SVGAttributeParser::parse<SkSVGFeMorphology::Operator>(SkSVGFeMorphology::Operator* op) {
  static constexpr std::tuple<const char*, SkSVGFeMorphology::Operator> gMap[] = {
      {"dilate", SkSVGFeMorphology::Operator::kDilate},
      {"erode", SkSVGFeMorphology::Operator::kErode},
  };

  return this->parseEnumMap(gMap, op) && this->parseEOSToken();
}

template <>
bool SVGAttributeParser::parse<SkSVGFeMorphology::Radius>(SkSVGFeMorphology::Radius* radius) {
  std::vector<SVGNumberType> values;
  if (!this->parse(&values)) {
    return false;
  }

  radius->fX = values[0];
  radius->fY = values.size() > 1 ? values[1] : values[0];
  return true;
}
}  // namespace tgfx