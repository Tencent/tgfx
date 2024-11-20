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

#include "tgfx/svg/node/SVGPattern.h"
#include <cstring>
#include <fstream>
#include <optional>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shader.h"
#include "tgfx/core/Surface.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {

SkSVGPattern::SkSVGPattern() : INHERITED(SVGTag::kPattern) {
}

bool SkSVGPattern::parseAndSetAttribute(const char* name, const char* value) {
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

#ifndef RENDER_SVG
const SkSVGPattern* SkSVGPattern::hrefTarget(const SVGRenderContext& ctx) const {
  if (fHref.iri().empty()) {
    return nullptr;
  }

  const auto href = ctx.findNodeById(fHref);
  if (!href || href->tag() != SVGTag::kPattern) {
    return nullptr;
  }

  return static_cast<const SkSVGPattern*>(href.get());
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
const SkSVGPattern* SkSVGPattern::resolveHref(const SVGRenderContext& ctx,
                                              PatternAttributes* attrs) const {
  const SkSVGPattern* currentNode = this;
  const SkSVGPattern* contentNode = this;

  do {
    // Bitwise OR to avoid short-circuiting.
    const bool didInherit =
        inherit_if_needed(currentNode->fX, attrs->fX) |
        inherit_if_needed(currentNode->fY, attrs->fY) |
        inherit_if_needed(currentNode->fWidth, attrs->fWidth) |
        inherit_if_needed(currentNode->fHeight, attrs->fHeight) |
        inherit_if_needed(currentNode->fPatternTransform, attrs->fPatternTransform);

    if (!contentNode->hasChildren()) {
      contentNode = currentNode;
    }

    if (contentNode->hasChildren() && !didInherit) {
      // All attributes have been resolved, and a valid content node has been found.
      // We can terminate the href chain early.
      break;
    }

    currentNode = currentNode->hrefTarget(ctx);
  } while (currentNode);

  // To unify with Chrome and macOS preview, the width and height attributes here need to be
  // converted to percentages, direct numbers are not supported.
  if (fPatternUnits.type() == SVGObjectBoundingBoxUnits::Type::kUserSpaceOnUse) {
    if (attrs->fWidth.has_value() && attrs->fWidth->unit() == SVGLength::Unit::kPercentage) {
      attrs->fWidth = SVGLength(attrs->fWidth->value() / 100.f, SVGLength::Unit::kNumber);
    }
    if (attrs->fHeight.has_value() && attrs->fHeight->unit() == SVGLength::Unit::kPercentage) {
      attrs->fHeight = SVGLength(attrs->fHeight->value() / 100.f, SVGLength::Unit::kNumber);
    }
  } else if (fPatternUnits.type() == SVGObjectBoundingBoxUnits::Type::kObjectBoundingBox) {
    if (attrs->fWidth.has_value() && attrs->fWidth->unit() == SVGLength::Unit::kNumber) {
      attrs->fWidth = SVGLength(attrs->fWidth->value() * 100.f, SVGLength::Unit::kPercentage);
    }
    if (attrs->fHeight.has_value() && attrs->fHeight->unit() == SVGLength::Unit::kNumber) {
      attrs->fHeight = SVGLength(attrs->fHeight->value() * 100.f, SVGLength::Unit::kPercentage);
    }
  }

  return contentNode;
}

bool SkSVGPattern::onAsPaint(const SVGRenderContext& ctx, Paint* paint) const {
  PatternAttributes attrs;
  const auto* contentNode = this->resolveHref(ctx, &attrs);
  auto lengthContext = ctx.lengthContext();
  lengthContext.setPatternUnits(fPatternUnits);
  Rect tile = lengthContext.resolveRect(attrs.fX.has_value() ? *attrs.fX : SVGLength(0),
                                        attrs.fY.has_value() ? *attrs.fY : SVGLength(0),
                                        attrs.fWidth.has_value() ? *attrs.fWidth : SVGLength(0),
                                        attrs.fHeight.has_value() ? *attrs.fHeight : SVGLength(0));

  if (tile.isEmpty()) {
    return false;
  }

  Recorder patternRecorder;
  auto* canvas = patternRecorder.beginRecording();
  auto patternMatrix = attrs.fPatternTransform.value_or(Matrix::I());
  canvas->concat(patternMatrix);
  {
    SVGRenderContext recordingContext(ctx, canvas, lengthContext);
    contentNode->SkSVGContainer::onRender(recordingContext);
  }
  auto picture = patternRecorder.finishRecordingAsPicture();
  auto shaderImage =
      Image::MakeFrom(picture, static_cast<int>(tile.width()), static_cast<int>(tile.height()));
  auto shader = Shader::MakeImageShader(shaderImage, TileMode::Repeat, TileMode::Repeat);
  paint->setShader(shader);
  return true;
}
#endif
}  // namespace tgfx