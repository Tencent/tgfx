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

#include "tgfx/svg/node/SVGFeComposite.h"
#include <tuple>
#include "core/utils/Log.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

class SVGRenderContext;

bool SkSVGFeComposite::parseAndSetAttribute(const char* name, const char* value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         // SkSVGFeInputType parsing defined in SkSVGFe.cpp:
         this->setIn2(SVGAttributeParser::parse<SVGFeInputType>("in2", name, value)) ||
         this->setK1(SVGAttributeParser::parse<SVGNumberType>("k1", name, value)) ||
         this->setK2(SVGAttributeParser::parse<SVGNumberType>("k2", name, value)) ||
         this->setK3(SVGAttributeParser::parse<SVGNumberType>("k3", name, value)) ||
         this->setK4(SVGAttributeParser::parse<SVGNumberType>("k4", name, value)) ||
         this->setOperator(
             SVGAttributeParser::parse<SVGFeCompositeOperator>("operator", name, value));
}

#ifndef RENDER_SVG
BlendMode SkSVGFeComposite::BlendModeForOperator(SVGFeCompositeOperator op) {
  switch (op) {
    case SVGFeCompositeOperator::kOver:
      return BlendMode::SrcOver;
    case SVGFeCompositeOperator::kIn:
      return BlendMode::SrcIn;
    case SVGFeCompositeOperator::kOut:
      return BlendMode::SrcOut;
    case SVGFeCompositeOperator::kAtop:
      return BlendMode::SrcATop;
    case SVGFeCompositeOperator::kXor:
      return BlendMode::Xor;
    case SVGFeCompositeOperator::kArithmetic:
      // Arithmetic is not handled with a blend
      ASSERT(false);
      return BlendMode::SrcOver;
  }
}

std::shared_ptr<ImageFilter> SkSVGFeComposite::onMakeImageFilter(
    const SVGRenderContext& /*ctx*/, const SkSVGFilterContext& /*fctx*/) const {
  // const Rect cropRect = this->resolveFilterSubregion(ctx, fctx);
  // const SVGColorspace colorspace = this->resolveColorspace(ctx, fctx);
  // const std::shared_ptr<ImageFilter> background = fctx.resolveInput(ctx, fIn2, colorspace);
  // const std::shared_ptr<ImageFilter> foreground = fctx.resolveInput(ctx, this->getIn(), colorspace);
  // if (fOperator == SVGFeCompositeOperator::kArithmetic) {
  //   constexpr bool enforcePMColor = true;
  //   return ImageFilter::Arithmetic(fK1, fK2, fK3, fK4, enforcePMColor, background, foreground,
  //                                     cropRect);
  // } else {
  //   return ImageFilter::Blend(BlendModeForOperator(fOperator), background, foreground, cropRect);
  // }

  //TODO (YG)
  return nullptr;
}
#endif  // RENDER_SVG

template <>
bool SVGAttributeParser::parse(SVGFeCompositeOperator* op) {
  static constexpr std::tuple<const char*, SVGFeCompositeOperator> gOpMap[] = {
      {"over", SVGFeCompositeOperator::kOver}, {"in", SVGFeCompositeOperator::kIn},
      {"out", SVGFeCompositeOperator::kOut},   {"atop", SVGFeCompositeOperator::kAtop},
      {"xor", SVGFeCompositeOperator::kXor},   {"arithmetic", SVGFeCompositeOperator::kArithmetic},
  };

  return this->parseEnumMap(gOpMap, op) && this->parseEOSToken();
}
}  // namespace tgfx