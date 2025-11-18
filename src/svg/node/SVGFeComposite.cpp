/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class SVGRenderContext;

bool SVGFeComposite::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setIn2(SVGAttributeParser::parse<SVGFeInputType>("in2", name, value)) ||
         this->setK1(SVGAttributeParser::parse<SVGNumberType>("k1", name, value)) ||
         this->setK2(SVGAttributeParser::parse<SVGNumberType>("k2", name, value)) ||
         this->setK3(SVGAttributeParser::parse<SVGNumberType>("k3", name, value)) ||
         this->setK4(SVGAttributeParser::parse<SVGNumberType>("k4", name, value)) ||
         this->setOperator(
             SVGAttributeParser::parse<SVGFeCompositeOperator>("operator", name, value));
}

BlendMode SVGFeComposite::BlendModeForOperator(SVGFeCompositeOperator op) {
  switch (op) {
    case SVGFeCompositeOperator::Over:
      return BlendMode::SrcOver;
    case SVGFeCompositeOperator::In:
      return BlendMode::SrcIn;
    case SVGFeCompositeOperator::Out:
      return BlendMode::SrcOut;
    case SVGFeCompositeOperator::Atop:
      return BlendMode::SrcATop;
    case SVGFeCompositeOperator::Xor:
      return BlendMode::Xor;
    case SVGFeCompositeOperator::Arithmetic:
      // Arithmetic is not handled with a blend
      ASSERT(false);
      return BlendMode::SrcOver;
  }
}

std::shared_ptr<ImageFilter> SVGFeComposite::onMakeImageFilter(
    const SVGRenderContext& /*context*/, const SVGFilterContext& /*filterContext*/) const {
  //TODO (YGAurora) waiting for blend and arithmetic image filter implementation
  return nullptr;
}

template <>
bool SVGAttributeParser::parse(SVGFeCompositeOperator* op) {
  static constexpr std::tuple<const char*, SVGFeCompositeOperator> opMap[] = {
      {"over", SVGFeCompositeOperator::Over}, {"in", SVGFeCompositeOperator::In},
      {"out", SVGFeCompositeOperator::Out},   {"atop", SVGFeCompositeOperator::Atop},
      {"xor", SVGFeCompositeOperator::Xor},   {"arithmetic", SVGFeCompositeOperator::Arithmetic},
  };

  return this->parseEnumMap(opMap, op) && this->parseEOSToken();
}
}  // namespace tgfx