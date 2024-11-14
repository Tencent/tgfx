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

#include "tgfx/svg/node/SVGFeComponentTransfer.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include "core/utils/Log.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

class SVGRenderContext;

#ifdef RENDER_SVG
sk_sp<SkImageFilter> SkSVGFeComponentTransfer::onMakeImageFilter(
    const SVGRenderContext& ctx, const SkSVGFilterContext& fctx) const {
  std::vector<uint8_t> a_tbl, b_tbl, g_tbl, r_tbl;

  for (const auto& child : fChildren) {
    switch (child->tag()) {
      case SkSVGTag::kFeFuncA:
        a_tbl = static_cast<const SkSVGFeFunc*>(child.get())->getTable();
        break;
      case SkSVGTag::kFeFuncB:
        b_tbl = static_cast<const SkSVGFeFunc*>(child.get())->getTable();
        break;
      case SkSVGTag::kFeFuncG:
        g_tbl = static_cast<const SkSVGFeFunc*>(child.get())->getTable();
        break;
      case SkSVGTag::kFeFuncR:
        r_tbl = static_cast<const SkSVGFeFunc*>(child.get())->getTable();
        break;
      default:
        break;
    }
  }
  SkASSERT(a_tbl.empty() || a_tbl.size() == 256);
  SkASSERT(b_tbl.empty() || b_tbl.size() == 256);
  SkASSERT(g_tbl.empty() || g_tbl.size() == 256);
  SkASSERT(r_tbl.empty() || r_tbl.size() == 256);

  const SkRect cropRect = this->resolveFilterSubregion(ctx, fctx);
  const sk_sp<SkImageFilter> input =
      fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx));

  const auto cf = SkColorFilters::TableARGB(
      a_tbl.empty() ? nullptr : a_tbl.data(), r_tbl.empty() ? nullptr : r_tbl.data(),
      g_tbl.empty() ? nullptr : g_tbl.data(), b_tbl.empty() ? nullptr : b_tbl.data());

  return SkImageFilters::ColorFilter(std::move(cf), std::move(input), cropRect);
}
#endif

std::vector<uint8_t> SkSVGFeFunc::getTable() const {
  // https://www.w3.org/TR/SVG11/filters.html#feComponentTransferTypeAttribute
  const auto make_linear = [this]() -> std::vector<uint8_t> {
    std::vector<uint8_t> tbl(256);
    const float slope = this->getSlope(), intercept255 = this->getIntercept() * 255;

    for (size_t i = 0; i < 256; ++i) {
      tbl[i] = static_cast<uint8_t>(
          std::clamp(std::round(intercept255 + static_cast<float>(i) * slope), 0.0f, 255.0f));
    }

    return tbl;
  };

  const auto make_gamma = [this]() -> std::vector<uint8_t> {
    std::vector<uint8_t> tbl(256);
    const float exponent = this->getExponent(), offset = this->getOffset();

    for (size_t i = 0; i < 256; ++i) {
      const float component = offset + std::pow(static_cast<float>(i) * (1 / 255.f), exponent);
      tbl[i] = static_cast<uint8_t>(std::clamp(std::round(component * 255), 0.0f, 255.0f));
    }

    return tbl;
  };

  const auto lerp_from_table_values = [this](auto lerp_func) -> std::vector<uint8_t> {
    const auto& vals = this->getTableValues();
    if (vals.size() < 2 || vals.size() > 255) {
      return {};
    }

    // number of interpolation intervals
    const size_t n = vals.size() - 1;

    std::vector<uint8_t> tbl(256);
    for (size_t k = 0; k < n; ++k) {
      // interpolation values
      const SVGNumberType v0 = std::clamp(vals[k + 0], 0.f, 1.f);
      const SVGNumberType v1 = std::clamp(vals[k + 1], 0.f, 1.f);

      // start/end component table indices
      const size_t c_start = k * 255 / n, c_end = (k + 1) * 255 / n;
      ASSERT(c_end <= 255);

      for (size_t ci = c_start; ci < c_end; ++ci) {
        const float lerp_t = static_cast<float>(ci - c_start) / static_cast<float>(c_end - c_start);
        const float component = lerp_func(v0, v1, lerp_t);
        ASSERT(component >= 0 && component <= 1);

        tbl[ci] = static_cast<uint8_t>(std::round(component * 255));
      }
    }

    tbl.back() = static_cast<uint8_t>(std::round(255 * std::clamp(vals.back(), 0.f, 1.f)));

    return tbl;
  };

  const auto make_table = [&]() -> std::vector<uint8_t> {
    return lerp_from_table_values([](float v0, float v1, float t) { return v0 + (v1 - v0) * t; });
  };

  const auto make_discrete = [&]() -> std::vector<uint8_t> {
    return lerp_from_table_values([](float v0, float /*v1*/, float /*t*/) { return v0; });
  };

  switch (this->getType()) {
    case SVGFeFuncType::kIdentity:
      return {};
    case SVGFeFuncType::kTable:
      return make_table();
    case SVGFeFuncType::kDiscrete:
      return make_discrete();
    case SVGFeFuncType::kLinear:
      return make_linear();
    case SVGFeFuncType::kGamma:
      return make_gamma();
  }

  //   SkUNREACHABLE;
}

bool SkSVGFeFunc::parseAndSetAttribute(const char* name, const char* val) {
  return INHERITED::parseAndSetAttribute(name, val) ||
         this->setAmplitude(SVGAttributeParser::parse<SVGNumberType>("amplitude", name, val)) ||
         this->setExponent(SVGAttributeParser::parse<SVGNumberType>("exponent", name, val)) ||
         this->setIntercept(SVGAttributeParser::parse<SVGNumberType>("intercept", name, val)) ||
         this->setOffset(SVGAttributeParser::parse<SVGNumberType>("offset", name, val)) ||
         this->setSlope(SVGAttributeParser::parse<SVGNumberType>("slope", name, val)) ||
         this->setTableValues(
             SVGAttributeParser::parse<std::vector<SVGNumberType>>("tableValues", name, val)) ||
         this->setType(SVGAttributeParser::parse<SVGFeFuncType>("type", name, val));
}

template <>
bool SVGAttributeParser::parse(SVGFeFuncType* type) {
  static constexpr std::tuple<const char*, SVGFeFuncType> gTypeMap[] = {
      {"identity", SVGFeFuncType::kIdentity}, {"table", SVGFeFuncType::kTable},
      {"discrete", SVGFeFuncType::kDiscrete}, {"linear", SVGFeFuncType::kLinear},
      {"gamma", SVGFeFuncType::kGamma},
  };

  return this->parseEnumMap(gTypeMap, type) && this->parseEOSToken();
}
}  // namespace tgfx