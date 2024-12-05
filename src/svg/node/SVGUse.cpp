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
#include <cstring>
#include "core/utils/MathExtra.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"
#include "tgfx/svg/SVGRenderContext.h"

namespace tgfx {

SkSVGUse::SkSVGUse() : INHERITED(SVGTag::Use) {
}

bool SkSVGUse::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setHref(SVGAttributeParser::parse<SVGIRI>("xlink:href", n, v));
}

#ifndef RENDER_SVG
bool SkSVGUse::onPrepareToRender(SVGRenderContext* ctx) const {
  if (fHref.iri().empty() || !INHERITED::onPrepareToRender(ctx)) {
    return false;
  }

  if (!FloatNearlyZero(fX.value()) || !FloatNearlyZero(fY.value())) {
    // Restored when the local SVGRenderContext leaves scope.
    ctx->saveOnce();
    ctx->canvas()->translate(fX.value(), fY.value());
  }

  // TODO: width/height override for <svg> targets.

  return true;
}

void SkSVGUse::onRender(const SVGRenderContext& ctx) const {
  const auto ref = ctx.findNodeById(fHref);
  if (!ref) {
    return;
  }

  auto lengthContext = ctx.lengthContext();
  lengthContext.clearPatternUnits();
  SVGRenderContext localContext(ctx, lengthContext);
  ref->render(localContext);
}

Path SkSVGUse::onAsPath(const SVGRenderContext& ctx) const {
  const auto ref = ctx.findNodeById(fHref);
  if (!ref) {
    return Path();
  }

  auto lengthContext = ctx.lengthContext();
  lengthContext.clearPatternUnits();
  SVGRenderContext localContext(ctx, lengthContext);
  return ref->asPath(localContext);
}

Rect SkSVGUse::onObjectBoundingBox(const SVGRenderContext& ctx) const {
  const auto ref = ctx.findNodeById(fHref);
  if (!ref) {
    return Rect::MakeEmpty();
  }

  auto lengthContext = ctx.lengthContext();
  lengthContext.clearPatternUnits();
  float x = lengthContext.resolve(fX, SVGLengthContext::LengthType::Horizontal);
  float y = lengthContext.resolve(fY, SVGLengthContext::LengthType::Vertical);

  Rect bounds = ref->objectBoundingBox(ctx);
  bounds.offset(x, y);

  return bounds;
}
#endif
}  // namespace tgfx