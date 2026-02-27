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

#include "tgfx/svg/node/SVGFeBlend.h"
#include <tuple>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

bool SVGFeBlend::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn2(SVGAttributeParser::parse<SVGFeInputType>("in2", name, value)) ||
         this->setBlendMode(SVGAttributeParser::parse<SVGFeBlend::Mode>("mode", name, value));
}

std::shared_ptr<ImageFilter> SVGFeBlend::onMakeImageFilter(
    const SVGRenderContext& context, const SVGFilterContext& filterContext) const {
  const SVGColorspace colorspace = this->resolveColorspace(context, filterContext);
  const std::shared_ptr<ImageFilter> background =
      filterContext.resolveInput(context, In2, colorspace);
  const std::shared_ptr<ImageFilter> foreground =
      filterContext.resolveInput(context, this->getIn(), colorspace);
  return ImageFilter::Compose(background, foreground);
  // TODO (YG),relay on ImageFilters::Blend to implement this
}

template <>
bool SVGAttributeParser::parse<SVGFeBlend::Mode>(SVGFeBlend::Mode* mode) {
  static constexpr std::tuple<const char*, SVGFeBlend::Mode> blendMap[] = {
      {"normal", SVGFeBlend::Mode::Normal},   {"multiply", SVGFeBlend::Mode::Multiply},
      {"screen", SVGFeBlend::Mode::Screen},   {"darken", SVGFeBlend::Mode::Darken},
      {"lighten", SVGFeBlend::Mode::Lighten},
  };

  return this->parseEnumMap(blendMap, mode) && this->parseEOSToken();
}
}  // namespace tgfx
