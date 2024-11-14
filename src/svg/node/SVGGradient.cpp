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

#include "tgfx/svg/node/SVGGradient.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include "core/utils/Log.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGGradient::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setGradientTransform(
             SVGAttributeParser::parse<SVGTransformType>("gradientTransform", name, value)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", name, value)) ||
         this->setSpreadMethod(
             SVGAttributeParser::parse<SVGSpreadMethod>("spreadMethod", name, value)) ||
         this->setGradientUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("gradientUnits", name, value));
}

#ifndef RENDER_SVG
// https://www.w3.org/TR/SVG11/pservers.html#LinearGradientElementHrefAttribute
void SkSVGGradient::collectColorStops(const SVGRenderContext& ctx, std::vector<Color>& colors,
                                      std::vector<float>& positions) const {
  // Used to resolve percentage offsets.
  const SVGLengthContext ltx(Size::Make(1, 1));

  this->forEachChild<SkSVGStop>([&](const SkSVGStop* stop) {
    colors.push_back(this->resolveStopColor(ctx, *stop));
    positions.push_back(
        std::clamp(ltx.resolve(stop->getOffset(), SVGLengthContext::LengthType::kOther), 0.f, 1.f));
  });

  ASSERT(colors.size() == positions.size());

  if (positions.empty() && !fHref.iri().empty()) {
    const auto ref = ctx.findNodeById(fHref);
    if (ref && (ref->tag() == SVGTag::kLinearGradient || ref->tag() == SVGTag::kRadialGradient)) {
      static_cast<const SkSVGGradient*>(ref.get())->collectColorStops(ctx, colors, positions);
    }
  }
}

Color SkSVGGradient::resolveStopColor(const SVGRenderContext& ctx, const SkSVGStop& stop) const {
  const auto& stopColor = stop.getStopColor();
  const auto& stopOpacity = stop.getStopOpacity();
  // Uninherited presentation attrs should have a concrete value at this point.
  if (!stopColor.isValue() || !stopOpacity.isValue()) {
    return Color::Black();
  }

  const auto color = ctx.resolveSvgColor(*stopColor);

  return {color.red, color.green, color.blue, *stopOpacity * color.alpha};
}

bool SkSVGGradient::onAsPaint(const SVGRenderContext& ctx, Paint* paint) const {
  std::vector<Color> colors;
  std::vector<float> positions;

  this->collectColorStops(ctx, colors, positions);

  // TODO:
  //       * stop (lazy?) sorting
  //       * href loop detection
  //       * href attribute inheritance (not just color stops)
  //       * objectBoundingBox units support

  static_assert(static_cast<TileMode>(SVGSpreadMethod::Type::kPad) == TileMode::Clamp,
                "SkSVGSpreadMethod::Type is out of sync");
  static_assert(static_cast<TileMode>(SVGSpreadMethod::Type::kRepeat) == TileMode::Repeat,
                "SkSVGSpreadMethod::Type is out of sync");
  static_assert(static_cast<TileMode>(SVGSpreadMethod::Type::kReflect) == TileMode::Mirror,
                "SkSVGSpreadMethod::Type is out of sync");
  const auto tileMode = static_cast<TileMode>(fSpreadMethod.type());

  const auto obbt = ctx.transformForCurrentOBB(fGradientUnits);
  const auto localMatrix = Matrix::MakeTrans(obbt.offset.x, obbt.offset.y) *
                           Matrix::MakeScale(obbt.scale.x, obbt.scale.y) * fGradientTransform;

  paint->setShader(this->onMakeShader(ctx, colors, positions, tileMode, localMatrix));
  return true;
}
#endif

// https://www.w3.org/TR/SVG11/pservers.html#LinearGradientElementSpreadMethodAttribute
template <>
bool SVGAttributeParser::parse(SVGSpreadMethod* spread) {
  struct TileInfo {
    SVGSpreadMethod::Type fType;
    const char* fName;
  };
  static const TileInfo spreadInfoSet[] = {
      {SVGSpreadMethod::Type::kPad, "pad"},
      {SVGSpreadMethod::Type::kReflect, "reflect"},
      {SVGSpreadMethod::Type::kRepeat, "repeat"},
  };

  bool parsedValue = false;
  for (auto info : spreadInfoSet) {
    if (this->parseExpectedStringToken(info.fName)) {
      *spread = SVGSpreadMethod(info.fType);
      parsedValue = true;
      break;
    }
  }

  return parsedValue && this->parseEOSToken();
}
}  // namespace tgfx