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

#include "tgfx/svg/node/SVGFeOffset.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGFeOffset::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setDx(SVGAttributeParser::parse<SVGNumberType>("dx", name, value)) ||
         this->setDy(SVGAttributeParser::parse<SVGNumberType>("dy", name, value));
}

#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> SkSVGFeOffset::onMakeImageFilter(
    const SVGRenderContext& /*ctx*/, const SkSVGFilterContext& /*fctx*/) const {
  // const auto d =
  //     SkV2{this->getDx(), this->getDy()} * ctx.transformForCurrentOBB(fctx.primitiveUnits()).scale;

  // sk_sp<SkImageFilter> in =
  //     fctx.resolveInput(ctx, this->getIn(), this->resolveColorspace(ctx, fctx));
  // return SkImageFilters::Offset(d.x, d.y, std::move(in), this->resolveFilterSubregion(ctx, fctx));

  //TODO (YG)
  return nullptr;
}
#endif
}  // namespace tgfx