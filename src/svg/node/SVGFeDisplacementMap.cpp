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

#include "tgfx/svg/node/SVGFeDisplacementMap.h"
#include <tuple>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

bool SVGFeDisplacementMap::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn2(SVGAttributeParser::parse<SVGFeInputType>("in2", name, value)) ||
         this->setXChannelSelector(SVGAttributeParser::parse<SVGFeDisplacementMap::ChannelSelector>(
             "xChannelSelector", name, value)) ||
         this->setYChannelSelector(SVGAttributeParser::parse<SVGFeDisplacementMap::ChannelSelector>(
             "yChannelSelector", name, value)) ||
         this->setScale(SVGAttributeParser::parse<SVGNumberType>("scale", name, value));
}

std::shared_ptr<ImageFilter> SVGFeDisplacementMap::onMakeImageFilter(
    const SVGRenderContext&, const SVGFilterContext&) const {
  //TODO (YGAurora): waiting for displacementMap image filter.
  return nullptr;
}

SVGColorspace SVGFeDisplacementMap::resolveColorspace(const SVGRenderContext& context,
                                                      const SVGFilterContext& filterContext) const {
  // According to spec https://www.w3.org/TR/SVG11/filters.html#feDisplacementMapElement,
  // the 'in' source image must remain in its current colorspace, which means the colorspace of
  // this FE node is the same as the input.
  return filterContext.resolveInputColorspace(context, this->getIn());
}

template <>
bool SVGAttributeParser::parse<SVGFeDisplacementMap::ChannelSelector>(
    SVGFeDisplacementMap::ChannelSelector* channel) {
  static constexpr std::tuple<const char*, SVGFeDisplacementMap::ChannelSelector> gMap[] = {
      {"R", SVGFeDisplacementMap::ChannelSelector::R},
      {"G", SVGFeDisplacementMap::ChannelSelector::G},
      {"B", SVGFeDisplacementMap::ChannelSelector::B},
      {"A", SVGFeDisplacementMap::ChannelSelector::A},
  };

  return this->parseEnumMap(gMap, channel) && this->parseEOSToken();
}
}  // namespace tgfx
