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

#include "tgfx/svg/node/SVGPattern.h"
#include <cstring>
#include <optional>
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

SVGPattern::SVGPattern() : INHERITED(SVGTag::Pattern) {
}

bool SVGPattern::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", name, value)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", name, value)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", name, value)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", name, value)) ||
         this->setPatternTransform(
             SVGAttributeParser::parse<SVGTransformType>("patternTransform", name, value)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", name, value)) ||
         this->setPatternUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("patternUnits", name, value)) ||
         this->setContentUnits(SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>(
             "patternContentUnits", name, value));
}

const SVGPattern* SVGPattern::hrefTarget(const SVGRenderContext& context) const {
  if (Href.iri().empty()) {
    return nullptr;
  }

  const auto href = context.findNodeById(Href);
  if (!href || href->tag() != SVGTag::Pattern) {
    return nullptr;
  }

  return static_cast<const SVGPattern*>(href.get());
}

template <typename T>
int inherit_if_needed(const std::optional<T>& src, std::optional<T>& dst) {
  if (!dst.has_value()) {
    dst = src;
    return 1;
  }

  return 0;
}

/* https://www.w3.org/TR/SVG11/pservers.html#PatternElementHrefAttribute
 *
 * Any attributes which are defined on the referenced element which are not defined on this element
 * are inherited by this element. If this element has no children, and the referenced element does
 * (possibly due to its own ‘xlink:href’ attribute), then this element inherits the children from
 * the referenced element. Inheritance can be indirect to an arbitrary level; thus, if the
 * referenced element inherits attributes or children due to its own ‘xlink:href’ attribute, then
 * the current element can inherit those attributes or children.
 */
const SVGPattern* SVGPattern::resolveHref(const SVGRenderContext& context,
                                          PatternAttributes* attrs) const {
  const SVGPattern* currentNode = this;
  const SVGPattern* contentNode = this;

  do {
    // Bitwise OR to avoid short-circuiting.
    const bool didInherit =
        inherit_if_needed(currentNode->X, attrs->x) | inherit_if_needed(currentNode->Y, attrs->y) |
        inherit_if_needed(currentNode->Width, attrs->width) |
        inherit_if_needed(currentNode->Height, attrs->height) |
        inherit_if_needed(currentNode->PatternTransform, attrs->patternTransform);

    if (!contentNode->hasChildren()) {
      contentNode = currentNode;
    }

    if (contentNode->hasChildren() && !didInherit) {
      // All attributes have been resolved, and a valid content node has been found.
      // We can terminate the href chain early.
      break;
    }

    currentNode = currentNode->hrefTarget(context);
  } while (currentNode);

  // To unify with Chrome and macOS preview, the width and height attributes here need to be
  // converted to percentages, direct numbers are not supported.
  if (PatternUnits.type() == SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse) {
    if (attrs->width.has_value() && attrs->width->unit() == SVGLength::Unit::Percentage) {
      attrs->width = SVGLength(attrs->width->value() / 100.f, SVGLength::Unit::Number);
    }
    if (attrs->height.has_value() && attrs->height->unit() == SVGLength::Unit::Percentage) {
      attrs->height = SVGLength(attrs->height->value() / 100.f, SVGLength::Unit::Number);
    }
  } else if (PatternUnits.type() == SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox) {
    if (attrs->width.has_value() && attrs->width->unit() == SVGLength::Unit::Number) {
      attrs->width = SVGLength(attrs->width->value() * 100.f, SVGLength::Unit::Percentage);
    }
    if (attrs->height.has_value() && attrs->height->unit() == SVGLength::Unit::Number) {
      attrs->height = SVGLength(attrs->height->value() * 100.f, SVGLength::Unit::Percentage);
    }
  }

  return contentNode;
}

bool SVGPattern::onAsPaint(const SVGRenderContext& context, Paint* paint) const {
  PatternAttributes attrs;
  const auto contentNode = this->resolveHref(context, &attrs);
  auto lengthContext = context.lengthContext();
  lengthContext.setBoundingBoxUnits(PatternUnits);
  Rect tile = lengthContext.resolveRect(attrs.x.has_value() ? *attrs.x : SVGLength(0),
                                        attrs.y.has_value() ? *attrs.y : SVGLength(0),
                                        attrs.width.has_value() ? *attrs.width : SVGLength(0),
                                        attrs.height.has_value() ? *attrs.height : SVGLength(0));

  if (tile.isEmpty()) {
    return false;
  }

  PictureRecorder patternRecorder;
  auto canvas = patternRecorder.beginRecording();
  auto patternMatrix = attrs.patternTransform.value_or(Matrix::I());
  canvas->concat(patternMatrix);
  {
    SVGRenderContext recordingContext(context, canvas, lengthContext);
    contentNode->SVGContainer::onRender(recordingContext);
  }
  auto picture = patternRecorder.finishRecordingAsPicture();
  auto shaderImage =
      Image::MakeFrom(picture, static_cast<int>(tile.width()), static_cast<int>(tile.height()));
  auto shader = Shader::MakeImageShader(shaderImage, TileMode::Repeat, TileMode::Repeat);
  paint->setShader(shader);
  return true;
}

}  // namespace tgfx