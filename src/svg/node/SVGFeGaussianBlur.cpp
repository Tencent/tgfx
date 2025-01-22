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
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "svg/SVGRenderContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Point.h"

namespace tgfx {

bool SVGFeGaussianBlur::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setstdDeviation(SVGAttributeParser::parse<SVGFeGaussianBlur::StdDeviation>(
             "stdDeviation", name, value));
}

std::shared_ptr<ImageFilter> SVGFeGaussianBlur::onMakeImageFilter(
    const SVGRenderContext& context, const SVGFilterContext& filterContext) const {
  auto scale = context.transformForCurrentBoundBox(filterContext.primitiveUnits()).scale;
  const auto sigmaX = stdDeviation.X * scale.x * 4 * context.matrix().getScaleX();
  const auto sigmaY = stdDeviation.Y * scale.y * 4 * context.matrix().getScaleY();
  return ImageFilter::Blur(sigmaX, sigmaY);
}

template <>
bool SVGAttributeParser::parse<SVGFeGaussianBlur::StdDeviation>(
    SVGFeGaussianBlur::StdDeviation* stdDeviation) {
  std::vector<SVGNumberType> values;
  if (!this->parse(&values)) {
    return false;
  }

  stdDeviation->X = values[0];
  stdDeviation->Y = values.size() > 1 ? values[1] : values[0];
  return true;
}
}  // namespace tgfx