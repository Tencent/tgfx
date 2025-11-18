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

#include "tgfx/svg/node/SVGGradient.h"
#include <algorithm>
#include <cstddef>
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

bool SVGGradient::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setGradientTransform(
             SVGAttributeParser::parse<SVGTransformType>("gradientTransform", name, value)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", name, value)) ||
         this->setSpreadMethod(
             SVGAttributeParser::parse<SVGSpreadMethod>("spreadMethod", name, value)) ||
         this->setGradientUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("gradientUnits", name, value));
}

// https://www.w3.org/TR/SVG11/pservers.html#LinearGradientElementHrefAttribute
void SVGGradient::collectColorStops(const SVGRenderContext& context, std::vector<Color>& colors,
                                    std::vector<float>& positions) const {
  // Used to resolve percentage offsets.
  const SVGLengthContext ltx(Size::Make(1, 1));

  this->forEachChild<SVGStop>([&](const SVGStop* stop) {
    colors.push_back(this->resolveStopColor(context, *stop));
    positions.push_back(
        std::clamp(ltx.resolve(stop->getOffset(), SVGLengthContext::LengthType::Other), 0.f, 1.f));
  });

  ASSERT(colors.size() == positions.size());

  if (positions.empty() && !Href.iri().empty()) {
    const auto ref = context.findNodeById(Href);
    if (ref && (ref->tag() == SVGTag::LinearGradient || ref->tag() == SVGTag::RadialGradient)) {
      static_cast<const SVGGradient*>(ref.get())->collectColorStops(context, colors, positions);
    }
  }
}

Color SVGGradient::resolveStopColor(const SVGRenderContext& context, const SVGStop& stop) const {
  const auto& stopColor = stop.getStopColor();
  const auto& stopOpacity = stop.getStopOpacity();
  // Uninherited presentation attrs should have a concrete value at this point.
  if (!stopColor.isValue() || !stopOpacity.isValue()) {
    return Color::Black();
  }

  const auto color = context.resolveSVGColor(*stopColor);

  return {color.red, color.green, color.blue, *stopOpacity * color.alpha};
}

bool SVGGradient::onAsPaint(const SVGRenderContext& context, Paint* paint) const {
  std::vector<Color> colors;
  std::vector<float> positions;

  this->collectColorStops(context, colors, positions);

  const auto tileMode = static_cast<TileMode>(SpreadMethod.type());

  const auto transform = context.transformForCurrentBoundBox(GradientUnits);
  const auto localMatrix = Matrix::MakeTrans(transform.offset.x, transform.offset.y) *
                           Matrix::MakeScale(transform.scale.x, transform.scale.y) *
                           GradientTransform;

  paint->setShader(this->onMakeShader(context, colors, positions, tileMode, localMatrix));
  return true;
}

// https://www.w3.org/TR/SVG11/pservers.html#LinearGradientElementSpreadMethodAttribute
template <>
bool SVGAttributeParser::parse(SVGSpreadMethod* spread) {
  struct TileInfo {
    SVGSpreadMethod::Type type;
    const char* name;
  };
  static const TileInfo spreadInfoSet[] = {
      {SVGSpreadMethod::Type::Pad, "pad"},
      {SVGSpreadMethod::Type::Reflect, "reflect"},
      {SVGSpreadMethod::Type::Repeat, "repeat"},
  };

  bool parsedValue = false;
  for (auto info : spreadInfoSet) {
    if (this->parseExpectedStringToken(info.name)) {
      *spread = SVGSpreadMethod(info.type);
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}
}  // namespace tgfx