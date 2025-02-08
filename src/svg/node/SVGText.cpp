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

#include "tgfx/svg/node/SVGText.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/core/Typeface.h"
#include "tgfx/core/UTF.h"
#include "tgfx/svg/SVGTypes.h"

namespace tgfx {
namespace {

std::vector<float> ResolveLengths(const SVGRenderContext& context,
                                  const std::vector<SVGLength>& lengths,
                                  SVGLengthContext::LengthType lengthType) {
  auto lengthContext = context.lengthContext();
  std::vector<float> resolved;
  resolved.reserve(lengths.size());

  for (const auto& length : lengths) {
    if (length.unit() == SVGLength::Unit::EMS || length.unit() == SVGLength::Unit::EXS) {
      auto fontSize = context.presentationContext()._inherited.FontSize->size();
      auto resolvedLength =
          lengthContext.resolve(fontSize, SVGLengthContext::LengthType::Horizontal) *
          length.value();
      resolved.push_back(resolvedLength);
    } else {
      resolved.push_back(lengthContext.resolve(length, lengthType));
    }
  }

  return resolved;
}

}  // namespace

void SVGTextContainer::appendChild(std::shared_ptr<SVGNode> child) {
  // Only allow text content child nodes.
  switch (child->tag()) {
    case SVGTag::TextLiteral:
    case SVGTag::TextPath:
    case SVGTag::TSpan:
      children.push_back(std::static_pointer_cast<SVGTextFragment>(child));
      break;
    default:
      break;
  }
}

void SVGTextFragment::renderText(const SVGRenderContext& context,
                                 const ShapedTextCallback& function) const {
  SVGRenderContext localContext(context);
  if (this->onPrepareToRender(&localContext)) {
    this->onShapeText(localContext, function);
  }
}

Path SVGTextFragment::onAsPath(const SVGRenderContext&) const {
  // TODO (YGAurora)
  return Path();
}

void SVGTextContainer::onShapeText(const SVGRenderContext& context,
                                   const ShapedTextCallback& function) const {

  auto x = ResolveLengths(context, X, SVGLengthContext::LengthType::Horizontal);
  auto y = ResolveLengths(context, Y, SVGLengthContext::LengthType::Vertical);
  auto dx = ResolveLengths(context, Dx, SVGLengthContext::LengthType::Horizontal);
  auto dy = ResolveLengths(context, Dy, SVGLengthContext::LengthType::Vertical);

  // TODO (YGAurora) : Handle rotate
  for (uint32_t i = 0; i < children.size(); i++) {
    auto child = children[i];
    context.canvas()->save();
    float offsetX = (i < x.size() ? x[i] : 0.0f) + (i < dx.size() ? dx[i] : 0.0f);
    float offsetY = (i < y.size() ? y[i] : 0.0f) + (i < dy.size() ? dy[i] : 0.0f);
    context.canvas()->translate(offsetX, offsetY);
    child->renderText(context, function);
    context.canvas()->restore();
  }
}

bool SVGTextContainer::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setX(SVGAttributeParser::parse<std::vector<SVGLength>>("x", name, value)) ||
         this->setY(SVGAttributeParser::parse<std::vector<SVGLength>>("y", name, value)) ||
         this->setDx(SVGAttributeParser::parse<std::vector<SVGLength>>("dx", name, value)) ||
         this->setDy(SVGAttributeParser::parse<std::vector<SVGLength>>("dy", name, value)) ||
         this->setRotate(
             SVGAttributeParser::parse<std::vector<SVGNumberType>>("rotate", name, value));
}

void SVGTextLiteral::onShapeText(const SVGRenderContext& context,
                                 const ShapedTextCallback& function) const {
  auto typeface = context.resolveTypeface();
  auto fallbackTypefaces = context.fallbackTypefaceList();
  float fontSize = context.resolveFontSize();

  std::vector<GlyphRun> glyphRuns = {};
  std::vector<GlyphID> glyphIDs = {};
  std::vector<Point> positions = {};

  Font previousFont = Font();
  auto emptyAdvance = fontSize / 2.0f;
  float XOffset = 0.0f;

  const char* textStart = Text.data();
  const char* textStop = textStart + Text.size();
  while (textStart < textStop) {
    const auto* oldPosition = textStart;
    UTF::NextUTF8(&textStart, textStop);
    auto length = textStart - oldPosition;
    auto str = std::string(oldPosition, static_cast<size_t>(length));

    GlyphID currentGlyphID = 0;
    std::shared_ptr<Typeface> currentTypeface = nullptr;

    // Match the string with SVG-parsed typeface, and try fallback typefaces if no match
    // is found
    GlyphID glyphID = typeface ? typeface->getGlyphID(str) : 0;
    if (glyphID != 0) {
      currentGlyphID = glyphID;
      currentTypeface = typeface;
    } else {
      for (const auto& typefaceItem : fallbackTypefaces) {
        if (typefaceItem == nullptr) {
          continue;
        }
        glyphID = typefaceItem->getGlyphID(str);
        if (glyphID != 0) {
          currentGlyphID = glyphID;
          currentTypeface = typefaceItem;
          break;
        }
      }
    }

    // Skip with empty advance if no matching typeface is found
    if (!currentTypeface) {
      XOffset += emptyAdvance;
      continue;
    }

    if (!previousFont.getTypeface()) {
      previousFont = Font(currentTypeface, fontSize);
    } else if (currentTypeface->uniqueID() != previousFont.getTypeface()->uniqueID()) {
      // If the current typeface differs from the previous one, create a glyph run from current
      // glyphs and start a new font
      glyphRuns.emplace_back(previousFont, glyphIDs, positions);
      glyphIDs.clear();
      positions.clear();
      previousFont = Font(currentTypeface, fontSize);
    }

    glyphIDs.emplace_back(currentGlyphID);
    positions.emplace_back(Point::Make(XOffset, 0.0f));
    XOffset += previousFont.getAdvance(currentGlyphID);
  }

  if (!glyphIDs.empty()) {
    glyphRuns.emplace_back(previousFont, glyphIDs, positions);
  }

  if (glyphRuns.empty()) {
    return;
  }
  auto textBlob = TextBlob::MakeFrom(glyphRuns);
  function(context, textBlob);
}

void SVGText::onRender(const SVGRenderContext& context) const {

  auto renderer = [](const SVGRenderContext& context,
                     const std::shared_ptr<TextBlob>& textBlob) -> void {
    if (!textBlob) {
      return;
    }

    auto bound = textBlob->getBounds();
    float x =
        context.presentationContext()._inherited.TextAnchor->getAlignmentFactor() * bound.width();
    float y = 0;

    auto lengthContext = context.lengthContext();
    lengthContext.setViewPort(Size::Make(bound.width(), bound.height()));
    SVGRenderContext paintContext(context, context.canvas(), lengthContext);
    auto fillPaint = paintContext.fillPaint();
    auto strokePaint = paintContext.strokePaint();

    if (fillPaint.has_value()) {
      context.canvas()->drawTextBlob(textBlob, x, y, fillPaint.value());
    }
    if (strokePaint.has_value()) {
      context.canvas()->drawTextBlob(textBlob, x, y, strokePaint.value());
    }
  };

  this->onShapeText(context, renderer);
}

Rect SVGText::onObjectBoundingBox(const SVGRenderContext& context) const {
  Rect bounds = Rect::MakeEmpty();

  auto boundCollector = [&bounds](const SVGRenderContext&,
                                  const std::shared_ptr<TextBlob>& textBlob) -> void {
    if (!textBlob) {
      return;
    }
    auto textBound = textBlob->getBounds();
    bounds.join(textBound);
  };

  this->onShapeText(context, boundCollector);
  return bounds;
}

Path SVGText::onAsPath(const SVGRenderContext&) const {
  // TODO (YGAurora)
  return Path();
}

void SVGTextPath::onShapeText(const SVGRenderContext& context,
                              const ShapedTextCallback& function) const {
  this->INHERITED::onShapeText(context, function);
}

bool SVGTextPath::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", name, value)) ||
         this->setStartOffset(SVGAttributeParser::parse<SVGLength>("startOffset", name, value));
}

}  // namespace tgfx