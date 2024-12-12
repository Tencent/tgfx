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

#include "tgfx/svg/node/SVGFilterContext.h"
#include <memory>
#include "core/utils/Log.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {
namespace {
#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> ConvertFilterColorspace(std::shared_ptr<ImageFilter>&& input,
                                                     SVGColorspace src, SVGColorspace dst) {
  if (src == dst) {
    return std::move(input);
  }
  // TODO (YG) - Implement this
  // else if (src == SVGColorspace::kSRGB && dst == SVGColorspace::kLinearRGB) {
  //   return ImageFilter::ColorFilter(SkColorFilters::SRGBToLinearGamma(), input);
  // } else {
  //   ASSERT(src == SVGColorspace::kLinearRGB && dst == SVGColorspace::kSRGB);
  //   return ImageFilter::ColorFilter(ColorFilters::LinearToSRGBGamma(), input);
  // }
  return std::move(input);
}

// std::shared_ptr<Shader> paint_as_shader(const Paint& paint) {
//   std::shared_ptr<Shader> shader = paint.getShader();
//   auto color = paint.getColor();
//   if (shader && color.alpha < 1.f) {
//     // Multiply by paint alpha
//     shader = shader->makeWithColorFilter(ColorFilter::Blend(color, BlendMode::DstIn));
//   } else if (!shader) {
//     shader = Shader::MakeColorShader(color);
//   }
//   if (paint.getColorFilter()) {
//     shader = shader->makeWithColorFilter(paint.getColorFilter());
//   }
//   return shader;
// }

#endif
}  // namespace

const SkSVGFilterContext::Result* SkSVGFilterContext::findResultById(
    const SVGStringType& id) const {
  auto iter = fResults.find(id);
  return iter != fResults.end() ? &iter->second : nullptr;
}

const Rect& SkSVGFilterContext::filterPrimitiveSubregion(const SVGFeInputType& input) const {
  const Result* res = nullptr;
  if (input.type() == SVGFeInputType::Type::FilterPrimitiveReference) {
    auto iter = fResults.find(input.id());
    res = iter != fResults.end() ? &iter->second : nullptr;
  } else if (input.type() == SVGFeInputType::Type::Unspecified) {
    res = &fPreviousResult;
  }
  return res ? res->fFilterSubregion : fFilterEffectsRegion;
}

void SkSVGFilterContext::registerResult(const SVGStringType& id,
                                        const std::shared_ptr<ImageFilter>& result,
                                        const Rect& subregion, SVGColorspace resultColorspace) {
  ASSERT(!id.empty());
  fResults[id] = {result, subregion, resultColorspace};
}

void SkSVGFilterContext::setPreviousResult(const std::shared_ptr<ImageFilter>& result,
                                           const Rect& subregion, SVGColorspace resultColorspace) {
  fPreviousResult = {result, subregion, resultColorspace};
}

bool SkSVGFilterContext::previousResultIsSourceGraphic() const {
  return fPreviousResult.fImageFilter == nullptr;
}

#ifndef RENDER_SVG
// https://www.w3.org/TR/SVG11/filters.html#FilterPrimitiveInAttribute
std::tuple<std::shared_ptr<ImageFilter>, SVGColorspace> SkSVGFilterContext::getInput(
    const SVGRenderContext& ctx, const SVGFeInputType& inputType) const {
  SVGColorspace inputCS = SVGColorspace::SRGB;
  std::shared_ptr<ImageFilter> result;
  switch (inputType.type()) {
    case SVGFeInputType::Type::SourceAlpha: {

      //TODO (YG) - Implement this with class ColorMatrix
      std::array<float, 20> colorMatrix{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
      auto colorFilter = ColorFilter::Matrix(colorMatrix);
      result = ImageFilter::ColorFilter(colorFilter);
      break;
    }
    case SVGFeInputType::Type::SourceGraphic:
      // Do nothing.
      break;
    case SVGFeInputType::Type::FillPaint: {
      const auto& fillPaint = ctx.fillPaint();
      if (fillPaint.has_value()) {
        //TODO (YG) - Implement this by dither and shader image filter
        // auto dither =
        //     fillPaint->isDither() ? SkImageFilters::Dither::kYes : SkImageFilters::Dither::kNo;
        // result = SkImageFilters::Shader(paint_as_shader(*fillPaint), dither);
      }
      break;
    }
    case SVGFeInputType::Type::StrokePaint: {
      // The paint filter doesn't apply fill/stroke styling, but use the paint settings
      // defined for strokes.
      const auto& strokePaint = ctx.strokePaint();
      if (strokePaint.has_value()) {
        //TODO (YG) - Implement this by dither and shader image filter
        // auto dither =
        //     strokePaint->isDither() ? SkImageFilters::Dither::kYes : SkImageFilters::Dither::kNo;
        // result = SkImageFilters::Shader(paint_as_shader(*strokePaint), dither);
      }
      break;
    }
    case SVGFeInputType::Type::FilterPrimitiveReference: {
      const Result* res = findResultById(inputType.id());
      if (res) {
        result = res->fImageFilter;
        inputCS = res->fColorspace;
      }
      break;
    }
    case SVGFeInputType::Type::Unspecified: {
      result = fPreviousResult.fImageFilter;
      inputCS = fPreviousResult.fColorspace;
      break;
    }
    default:
      break;
  }

  return {result, inputCS};
}

SVGColorspace SkSVGFilterContext::resolveInputColorspace(const SVGRenderContext& ctx,
                                                         const SVGFeInputType& inputType) const {
  return std::get<1>(this->getInput(ctx, inputType));
}

std::shared_ptr<ImageFilter> SkSVGFilterContext::resolveInput(
    const SVGRenderContext& ctx, const SVGFeInputType& inputType) const {
  return std::get<0>(this->getInput(ctx, inputType));
}

std::shared_ptr<ImageFilter> SkSVGFilterContext::resolveInput(const SVGRenderContext& ctx,
                                                              const SVGFeInputType& inputType,
                                                              SVGColorspace colorspace) const {
  auto [result, inputCS] = this->getInput(ctx, inputType);
  return ConvertFilterColorspace(std::move(result), inputCS, colorspace);
}
#endif
}  // namespace tgfx