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

#include "tgfx/svg/node/SVGFeGaussianBlur.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGFeGaussianBlur::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setStdDeviation(SVGAttributeParser::parse<SkSVGFeGaussianBlur::StdDeviation>(
             "stdDeviation", name, value));
}

#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> SkSVGFeGaussianBlur::onMakeImageFilter(
    const SVGRenderContext& ctx, const SkSVGFilterContext& fctx) const {
  auto scale = ctx.transformForCurrentOBB(fctx.primitiveUnits()).scale;
  const auto sigmaX = fStdDeviation.fX * scale.x * 4;
  const auto sigmaY = fStdDeviation.fY * scale.y * 4;
  return ImageFilter::Blur(sigmaX, sigmaY);
}
#endif

template <>
bool SVGAttributeParser::parse<SkSVGFeGaussianBlur::StdDeviation>(
    SkSVGFeGaussianBlur::StdDeviation* stdDeviation) {
  std::vector<SVGNumberType> values;
  if (!this->parse(&values)) {
    return false;
  }

  stdDeviation->fX = values[0];
  stdDeviation->fY = values.size() > 1 ? values[1] : values[0];
  return true;
}
}  // namespace tgfx