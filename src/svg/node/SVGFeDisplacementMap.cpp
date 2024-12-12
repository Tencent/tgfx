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

#include "tgfx/svg/node/SVGFeDisplacementMap.h"
#include <tuple>
#include "svg/SVGAttributeParser.h"
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
    const SVGRenderContext& ctx, const SVGFilterContext& fctx) const {
  const Rect cropRect = this->resolveFilterSubregion(ctx, fctx);
  cropRect.centerX();
  const SVGColorspace colorspace = this->resolveColorspace(ctx, fctx);

  // According to spec https://www.w3.org/TR/SVG11/filters.html#feDisplacementMapElement,
  // the 'in' source image must remain in its current colorspace.
  std::shared_ptr<ImageFilter> in = fctx.resolveInput(ctx, this->getIn());
  std::shared_ptr<ImageFilter> in2 = fctx.resolveInput(ctx, this->getIn2(), colorspace);

  float scale = Scale;
  if (fctx.primitiveUnits().type() == SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox) {
    const auto obbt = ctx.transformForCurrentOBB(fctx.primitiveUnits());
    scale = SVGLengthContext({obbt.scale.x, obbt.scale.y})
                .resolve(SVGLength(scale, SVGLength::Unit::Percentage),
                         SVGLengthContext::LengthType::Other);
  }

  return nullptr;
}

SVGColorspace SVGFeDisplacementMap::resolveColorspace(const SVGRenderContext& ctx,
                                                      const SVGFilterContext& fctx) const {
  // According to spec https://www.w3.org/TR/SVG11/filters.html#feDisplacementMapElement,
  // the 'in' source image must remain in its current colorspace, which means the colorspace of
  // this FE node is the same as the input.
  return fctx.resolveInputColorspace(ctx, this->getIn());
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