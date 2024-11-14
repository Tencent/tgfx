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

#include "tgfx/svg/node/SVGFeBlend.h"
#include <tuple>
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

class SVGRenderContext;

bool SkSVGFeBlend::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn2(SVGAttributeParser::parse<SVGFeInputType>("in2", name, value)) ||
         this->setMode(SVGAttributeParser::parse<SkSVGFeBlend::Mode>("mode", name, value));
}

#ifndef RENDER_SVG

// static BlendMode GetBlendMode(SkSVGFeBlend::Mode mode) {
//   switch (mode) {
//     case SkSVGFeBlend::Mode::kNormal:
//       return BlendMode::SrcOver;
//     case SkSVGFeBlend::Mode::kMultiply:
//       return BlendMode::Multiply;
//     case SkSVGFeBlend::Mode::kScreen:
//       return BlendMode::Screen;
//     case SkSVGFeBlend::Mode::kDarken:
//       return BlendMode::Darken;
//     case SkSVGFeBlend::Mode::kLighten:
//       return BlendMode::Lighten;
//   }
// }

std::shared_ptr<ImageFilter> SkSVGFeBlend::onMakeImageFilter(const SVGRenderContext& ctx,
                                                             const SkSVGFilterContext& fctx) const {
  // const Rect cropRect = this->resolveFilterSubregion(ctx, fctx);
  // const BlendMode blendMode = GetBlendMode(this->getMode());
  const SVGColorspace colorspace = this->resolveColorspace(ctx, fctx);
  const std::shared_ptr<ImageFilter> background = fctx.resolveInput(ctx, fIn2, colorspace);
  const std::shared_ptr<ImageFilter> foreground = fctx.resolveInput(ctx, this->getIn(), colorspace);
  return ImageFilter::Compose(background, foreground);
  // TODO (YG),relay on ImageFilters::Blend to implement this
  // return SkImageFilters::Blend(blendMode, background, foreground, cropRect);
}
#endif

template <>
bool SVGAttributeParser::parse<SkSVGFeBlend::Mode>(SkSVGFeBlend::Mode* mode) {
  static constexpr std::tuple<const char*, SkSVGFeBlend::Mode> gMap[] = {
      {"normal", SkSVGFeBlend::Mode::kNormal},   {"multiply", SkSVGFeBlend::Mode::kMultiply},
      {"screen", SkSVGFeBlend::Mode::kScreen},   {"darken", SkSVGFeBlend::Mode::kDarken},
      {"lighten", SkSVGFeBlend::Mode::kLighten},
  };

  return this->parseEnumMap(gMap, mode) && this->parseEOSToken();
}
}  // namespace tgfx