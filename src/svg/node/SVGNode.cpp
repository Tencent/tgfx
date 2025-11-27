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

#include "tgfx/svg/node/SVGNode.h"
#include <algorithm>
#include <cstddef>
#include <optional>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGNodeConstructor.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/PathTypes.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"
#include "tgfx/svg/xml/XMLDOM.h"

namespace tgfx {

SVGNode::SVGNode(SVGTag t) : _tag(t) {
  // Uninherited presentation attributes need a non-null default value.
  presentationAttributes.StopColor.set(SVGColor(Color::Black()));
  presentationAttributes.StopOpacity.set(static_cast<SVGNumberType>(1.0f));
  presentationAttributes.FloodColor.set(SVGColor(Color::Black()));
  presentationAttributes.FloodOpacity.set(static_cast<SVGNumberType>(1.0f));
  presentationAttributes.LightingColor.set(SVGColor(Color::White()));
}

SVGNode::~SVGNode() = default;

void SVGNode::render(const SVGRenderContext& context) const {
  SVGRenderContext localContext(context, this);

  if (this->onPrepareToRender(&localContext)) {
    this->onRender(localContext);
  }
}

bool SVGNode::asPaint(const SVGRenderContext& context, Paint* paint) const {
  SVGRenderContext localContext(context);

  return this->onPrepareToRender(&localContext) && this->onAsPaint(localContext, paint);
}

Path SVGNode::asPath(const SVGRenderContext& context) const {
  SVGRenderContext localContext(context);
  if (!this->onPrepareToRender(&localContext)) {
    return Path();
  }

  Path path = this->onAsPath(localContext);
  if (auto clipPath = localContext.clipPath(); !clipPath.isEmpty()) {
    // There is a clip-path present on the current node.
    path.addPath(clipPath, PathOp::Intersect);
  }

  return path;
}

Rect SVGNode::objectBoundingBox(const SVGRenderContext& context) const {
  return this->onObjectBoundingBox(context);
}

bool SVGNode::onPrepareToRender(SVGRenderContext* context) const {
  // Apply the inheritance of presentation attributes
  context->applyPresentationAttributes(
      presentationAttributes,
      this->hasChildren() ? 0 : static_cast<uint32_t>(SVGRenderContext::ApplyFlags::Leaf));

  // visibility:hidden and display:none disable rendering.
  const auto visibility = context->presentationContext()._inherited.Visibility->type();
  // display is uninherited
  const auto display = presentationAttributes.Display;
  return visibility != SVGVisibility::Type::Hidden &&
         (!display.isValue() || *display != SVGDisplay::None);
}

void SVGNode::setAttribute(SVGAttribute attribute, const SVGValue& value) {
  this->onSetAttribute(attribute, value);
}

bool SVGNode::setAttribute(const std::string& attributeName, const std::string& attributeValue) {
  return SVGNodeConstructor::SetAttribute(*this, attributeName, attributeValue, nullptr);
}

template <typename T>
void SetInheritedByDefault(std::optional<T>& presentation_attribute, const T& value) {
  if (value.type() != T::Type::kInherit) {
    presentation_attribute.set(value);
  } else {
    // kInherited values are semantically equivalent to
    // the absence of a local presentation attribute.
    presentation_attribute.reset();
  }
}

bool SVGNode::parseAndSetAttribute(const std::string& name, const std::string& value) {
#define PARSE_AND_SET(svgName, attrName)                                                          \
  this->set##attrName(                                                                            \
      SVGAttributeParser::parseProperty<decltype(presentationAttributes.attrName)>(svgName, name, \
                                                                                   value))

  return PARSE_AND_SET("clip-path", ClipPath) || PARSE_AND_SET("clip-rule", ClipRule) ||
         PARSE_AND_SET("color", Color) || PARSE_AND_SET("class", Class) ||
         PARSE_AND_SET("color-interpolation", ColorInterpolation) ||
         PARSE_AND_SET("color-interpolation-filters", ColorInterpolationFilters) ||
         PARSE_AND_SET("display", Display) || PARSE_AND_SET("fill", Fill) ||
         PARSE_AND_SET("fill-opacity", FillOpacity) || PARSE_AND_SET("fill-rule", FillRule) ||
         PARSE_AND_SET("filter", Filter) || PARSE_AND_SET("flood-color", FloodColor) ||
         PARSE_AND_SET("flood-opacity", FloodOpacity) || PARSE_AND_SET("font-family", FontFamily) ||
         PARSE_AND_SET("font-size", FontSize) || PARSE_AND_SET("font-style", FontStyle) ||
         PARSE_AND_SET("font-weight", FontWeight) || PARSE_AND_SET("id", ID) ||
         PARSE_AND_SET("lighting-color", LightingColor) || PARSE_AND_SET("mask", Mask) ||
         PARSE_AND_SET("opacity", Opacity) || PARSE_AND_SET("stop-color", StopColor) ||
         PARSE_AND_SET("stop-opacity", StopOpacity) || PARSE_AND_SET("stroke", Stroke) ||
         PARSE_AND_SET("stroke-dasharray", StrokeDashArray) ||
         PARSE_AND_SET("stroke-dashoffset", StrokeDashOffset) ||
         PARSE_AND_SET("stroke-linecap", StrokeLineCap) ||
         PARSE_AND_SET("stroke-linejoin", StrokeLineJoin) ||
         PARSE_AND_SET("stroke-miterlimit", StrokeMiterLimit) ||
         PARSE_AND_SET("stroke-opacity", StrokeOpacity) ||
         PARSE_AND_SET("stroke-width", StrokeWidth) || PARSE_AND_SET("text-anchor", TextAnchor) ||
         PARSE_AND_SET("visibility", Visibility);

#undef PARSE_AND_SET
}

// https://www.w3.org/TR/SVG11/coords.html#PreserveAspectRatioAttribute
Matrix SVGNode::ComputeViewboxMatrix(const Rect& viewBox, const Rect& viewPort,
                                     SVGPreserveAspectRatio PreAspectRatio) {
  if (viewBox.isEmpty() || viewPort.isEmpty()) {
    return Matrix::MakeScale(0, 0);
  }

  auto computeScale = [&]() -> Point {
    const auto scaleX = viewPort.width() / viewBox.width();
    const auto scaleY = viewPort.height() / viewBox.height();

    if (PreAspectRatio.align == SVGPreserveAspectRatio::Align::None) {
      // none -> anisotropic scaling, regardless of fScale
      return {scaleX, scaleY};
    }

    // isotropic scaling
    const auto s = PreAspectRatio.scale == SVGPreserveAspectRatio::Scale::Meet
                       ? std::min(scaleX, scaleY)
                       : std::max(scaleX, scaleY);
    return {s, s};
  };

  auto computeTrans = [&](const Point& scale) -> Point {
    static constexpr float alignCoeffs[] = {
        0.0f,  // Min
        0.5f,  // Mid
        1.0f   // Max
    };

    const size_t x_coeff = static_cast<int>(PreAspectRatio.align) >> 0 & 0x03;
    const size_t y_coeff = static_cast<int>(PreAspectRatio.align) >> 2 & 0x03;

    const auto tx = -viewBox.x() * scale.x;
    const auto ty = -viewBox.y() * scale.y;
    const auto dx = viewPort.width() - viewBox.width() * scale.x;
    const auto dy = viewPort.height() - viewBox.height() * scale.y;

    return {tx + dx * alignCoeffs[x_coeff], ty + dy * alignCoeffs[y_coeff]};
  };

  auto scale = computeScale();
  auto transform = computeTrans(scale);

  return Matrix::MakeTrans(transform.x, transform.y) * Matrix::MakeScale(scale.x, scale.y);
}

void SVGNode::addCustomAttribute(const std::string& name, const std::string& value) {
  customAttributes.push_back({name, value});
}

const std::vector<DOMAttribute>& SVGNode::getCustomAttributes() const {
  return customAttributes;
}

}  // namespace tgfx