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
#include "core/utils/Log.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/svg/SVGFontManager.h"

namespace tgfx {
namespace {

std::vector<float> ResolveLengths(const SVGLengthContext& lengthCtx,
                                  const std::vector<SVGLength>& lengths,
                                  SVGLengthContext::LengthType lengthType) {
  std::vector<float> resolved;
  resolved.reserve(lengths.size());

  for (const auto& length : lengths) {
    resolved.push_back(lengthCtx.resolve(length, lengthType));
  }

  return resolved;
}

float ComputeAlignmentFactor(const SVGPresentationContext& context) {
  switch (context._inherited.TextAnchor->type()) {
    case SVGTextAnchor::Type::Start:
      return 0.0f;
    case SVGTextAnchor::Type::Middle:
      return -0.5f;
    case SVGTextAnchor::Type::End:
      return -1.0f;
    case SVGTextAnchor::Type::Inherit:
      ASSERT(false);
      return 0.0f;
  }
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

  auto x = ResolveLengths(context.lengthContext(), X, SVGLengthContext::LengthType::Horizontal);
  auto y = ResolveLengths(context.lengthContext(), Y, SVGLengthContext::LengthType::Vertical);
  auto dx = ResolveLengths(context.lengthContext(), Dx, SVGLengthContext::LengthType::Horizontal);
  auto dy = ResolveLengths(context.lengthContext(), Dy, SVGLengthContext::LengthType::Vertical);

  // TODO (YGAurora) : Handle rotate
  for (uint32_t i = 0; i < children.size(); i++) {
    auto child = children[i];
    context.canvas()->save();
    float offsetX = x[i] + (i < dx.size() ? dx[i] : 0);
    float offsetY = y[i] + (i < dy.size() ? dy[i] : 0);
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

  auto [success, font] = context.resolveFont();
  if (!success) {
    return;
  }
  auto textBlob = TextBlob::MakeFrom(Text, font);
  function(context, textBlob);
}

void SVGText::onRender(const SVGRenderContext& context) const {

  auto renderer = [](const SVGRenderContext& renderCtx,
                     const std::shared_ptr<TextBlob>& textBlob) -> void {
    if (!textBlob) {
      return;
    }

    auto bound = textBlob->getBounds();
    float x = ComputeAlignmentFactor(renderCtx.presentationContext()) * bound.width();
    float y = 0;

    auto lengthCtx = renderCtx.lengthContext();
    lengthCtx.setViewPort(Size::Make(bound.width(), bound.height()));
    SVGRenderContext paintCtx(renderCtx, renderCtx.canvas(), lengthCtx);
    const auto fillPaint = paintCtx.fillPaint();
    const auto strokePaint = paintCtx.strokePaint();

    if (fillPaint.has_value()) {
      renderCtx.canvas()->drawTextBlob(textBlob, x, y, fillPaint.value());
    }
    if (strokePaint.has_value()) {
      renderCtx.canvas()->drawTextBlob(textBlob, x, y, strokePaint.value());
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