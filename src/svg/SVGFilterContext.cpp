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

#include "SVGFilterContext.h"
#include <memory>
#include "../../include/tgfx/core/Log.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

const SVGFilterContext::Result* SVGFilterContext::findResultById(const SVGStringType& ID) const {
  auto iter = results.find(ID);
  return iter != results.end() ? &iter->second : nullptr;
}

const Rect& SVGFilterContext::filterPrimitiveSubregion(const SVGFeInputType& input) const {
  const Result* res = nullptr;
  if (input.type() == SVGFeInputType::Type::FilterPrimitiveReference) {
    auto iter = results.find(input.id());
    res = iter != results.end() ? &iter->second : nullptr;
  } else if (input.type() == SVGFeInputType::Type::Unspecified) {
    res = &previousResult;
  }
  return res ? res->filterSubregion : _filterEffectsRegion;
}

void SVGFilterContext::registerResult(const SVGStringType& ID,
                                      const std::shared_ptr<ImageFilter>& result,
                                      const Rect& subregion, SVGColorspace resultColorspace) {
  ASSERT(!ID.empty());
  results[ID] = {result, subregion, resultColorspace};
}

void SVGFilterContext::setPreviousResult(const std::shared_ptr<ImageFilter>& result,
                                         const Rect& subregion, SVGColorspace resultColorspace) {
  previousResult = {result, subregion, resultColorspace};
}

bool SVGFilterContext::previousResultIsSourceGraphic() const {
  return previousResult.imageFilter == nullptr;
}

// https://www.w3.org/TR/SVG11/filters.html#FilterPrimitiveInAttribute
std::tuple<std::shared_ptr<ImageFilter>, SVGColorspace> SVGFilterContext::getInput(
    const SVGRenderContext& context, const SVGFeInputType& inputType) const {
  SVGColorspace inputCS = SVGColorspace::SRGB;
  std::shared_ptr<ImageFilter> result;
  switch (inputType.type()) {
    case SVGFeInputType::Type::SourceAlpha: {
      std::array<float, 20> colorMatrix{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
      auto colorFilter = ColorFilter::Matrix(colorMatrix);
      result = ImageFilter::ColorFilter(colorFilter);
      break;
    }
    case SVGFeInputType::Type::SourceGraphic:
      // Do nothing.
      break;
    case SVGFeInputType::Type::FillPaint: {
      const auto& fillPaint = context.fillPaint();
      if (fillPaint.has_value()) {
        auto colorFilter = ColorFilter::Blend(fillPaint->getColor(), BlendMode::DstIn);
        result = ImageFilter::ColorFilter(colorFilter);
      }
      break;
    }
    case SVGFeInputType::Type::StrokePaint: {
      const auto& strokePaint = context.strokePaint();
      if (strokePaint.has_value()) {
        auto colorFilter = ColorFilter::Blend(strokePaint->getColor(), BlendMode::DstIn);
        result = ImageFilter::ColorFilter(colorFilter);
      }
      break;
    }
    case SVGFeInputType::Type::FilterPrimitiveReference: {
      const Result* res = findResultById(inputType.id());
      if (res) {
        result = res->imageFilter;
        inputCS = res->colorspace;
      }
      break;
    }
    case SVGFeInputType::Type::Unspecified: {
      result = previousResult.imageFilter;
      inputCS = previousResult.colorspace;
      break;
    }
    default:
      break;
  }

  return {result, inputCS};
}

SVGColorspace SVGFilterContext::resolveInputColorspace(const SVGRenderContext& context,
                                                       const SVGFeInputType& inputType) const {
  return std::get<1>(this->getInput(context, inputType));
}

std::shared_ptr<ImageFilter> SVGFilterContext::resolveInput(const SVGRenderContext& context,
                                                            const SVGFeInputType& inputType) const {
  return std::get<0>(this->getInput(context, inputType));
}

std::shared_ptr<ImageFilter> SVGFilterContext::resolveInput(
    const SVGRenderContext& context, const SVGFeInputType& inputType,
    SVGColorspace /*resultColorspace*/) const {
  //TODO (YGAurora) - waiting for color space implementation
  return resolveInput(context, inputType);
}

}  // namespace tgfx