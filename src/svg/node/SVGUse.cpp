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

#include "tgfx/svg/node/SVGUse.h"
#include "core/utils/MathExtra.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

SVGUse::SVGUse() : INHERITED(SVGTag::Use) {
}

bool SVGUse::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", n, v));
}

bool SVGUse::onPrepareToRender(SVGRenderContext* ctx) const {
  if (Href.iri().empty() || !INHERITED::onPrepareToRender(ctx)) {
    return false;
  }

  if (!FloatNearlyZero(X.value()) || !FloatNearlyZero(Y.value())) {
    // Restored when the local SVGRenderContext leaves scope.
    ctx->saveOnce();
    ctx->canvas()->translate(X.value(), Y.value());
  }

  return true;
}

void SVGUse::onRender(const SVGRenderContext& ctx) const {
  const auto ref = ctx.findNodeById(Href);
  if (!ref) {
    return;
  }

  auto lengthContext = ctx.lengthContext();
  lengthContext.clearPatternUnits();
  SVGRenderContext localContext(ctx, lengthContext);
  ref->render(localContext);
}

Path SVGUse::onAsPath(const SVGRenderContext& ctx) const {
  const auto ref = ctx.findNodeById(Href);
  if (!ref) {
    return Path();
  }

  auto lengthContext = ctx.lengthContext();
  lengthContext.clearPatternUnits();
  SVGRenderContext localContext(ctx, lengthContext);
  return ref->asPath(localContext);
}

Rect SVGUse::onObjectBoundingBox(const SVGRenderContext& ctx) const {
  const auto ref = ctx.findNodeById(Href);
  if (!ref) {
    return Rect::MakeEmpty();
  }

  auto lengthContext = ctx.lengthContext();
  lengthContext.clearPatternUnits();
  float x = lengthContext.resolve(X, SVGLengthContext::LengthType::Horizontal);
  float y = lengthContext.resolve(Y, SVGLengthContext::LengthType::Vertical);

  Rect bounds = ref->objectBoundingBox(ctx);
  bounds.offset(x, y);

  return bounds;
}

}  // namespace tgfx