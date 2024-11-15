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

#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Paint.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGMask::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGLength>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGLength>("y", n, v)) ||
         this->setWidth(SVGAttributeParser::parse<SVGLength>("width", n, v)) ||
         this->setHeight(SVGAttributeParser::parse<SVGLength>("height", n, v)) ||
         this->setMaskUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("maskUnits", n, v)) ||
         this->setMaskContentUnits(
             SVGAttributeParser::parse<SVGObjectBoundingBoxUnits>("maskContentUnits", n, v));
}

#ifndef RENDER_SVG
Rect SkSVGMask::bounds(const SVGRenderContext& ctx) const {
  return ctx.resolveOBBRect(fX, fY, fWidth, fHeight, fMaskUnits);
}

void SkSVGMask::renderMask(const SVGRenderContext& ctx) const {
  //TODO (YG)

  // https://www.w3.org/TR/SVG11/masking.html#Masking

  // Propagate any inherited properties that may impact mask effect behavior (e.g.
  // color-interpolation). We call this explicitly here because the SkSVGMask
  // nodes do not participate in the normal onRender path, which is when property
  // propagation currently occurs.
  // The local context also restores the filter layer created below on scope exit.
  SVGRenderContext lctx(ctx);
  this->onPrepareToRender(&lctx);

  // const auto ci = *lctx.presentationContext()._inherited.fColorInterpolation;
  // auto ci_filter = (ci == SVGColorspace::kLinearRGB) ? ColorFilters::SRGBToLinearGamma() : nullptr;

  // Paint mask_filter;
  // MaskFilter::Compose();
  //     mask_filter.setMaskFilter() mask_filter.setColorFilter(
  //         SkColorFilters::Compose(SkLumaColorFilter::Make(), std::move(ci_filter)));

  // // Mask color filter layer.
  // // Note: We could avoid this extra layer if we invert the stacking order
  // // (mask/content -> content/mask, kSrcIn -> kDstIn) and apply the filter
  // // via the top (mask) layer paint.  That requires deferring mask rendering
  // // until after node content, which introduces extra state/complexity.
  // // Something to consider if masking performance ever becomes an issue.
  // lctx.canvas()->saveLayer(nullptr, &mask_filter);

  // const auto obbt = ctx.transformForCurrentOBB(fMaskContentUnits);
  // lctx.canvas()->translate(obbt.offset.x, obbt.offset.y);
  // lctx.canvas()->scale(obbt.scale.x, obbt.scale.y);

  // for (const auto& child : fChildren) {
  //   child->render(lctx);
  // }
}
#endif
}  // namespace tgfx