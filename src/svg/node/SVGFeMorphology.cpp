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

#include "tgfx/svg/node/SVGFeMorphology.h"
#include <tuple>
#include "svg/SVGAttributeParser.h"

namespace tgfx {

bool SVGFeMorphology::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setMorphOperator(
             SVGAttributeParser::parse<SVGFeMorphology::Operator>("operator", name, value)) ||
         this->setMorphRadius(
             SVGAttributeParser::parse<SVGFeMorphology::Radius>("radius", name, value));
}

std::shared_ptr<ImageFilter> SVGFeMorphology::onMakeImageFilter(const SVGRenderContext&,
                                                                const SVGFilterContext&) const {
  //TODO (YGAurora) waiting for morphology image filter.
  return nullptr;
}

template <>
bool SVGAttributeParser::parse<SVGFeMorphology::Operator>(SVGFeMorphology::Operator* op) {
  static constexpr std::tuple<const char*, SVGFeMorphology::Operator> gMap[] = {
      {"dilate", SVGFeMorphology::Operator::kDilate},
      {"erode", SVGFeMorphology::Operator::kErode},
  };

  return this->parseEnumMap(gMap, op) && this->parseEOSToken();
}

template <>
bool SVGAttributeParser::parse<SVGFeMorphology::Radius>(SVGFeMorphology::Radius* radius) {
  std::vector<SVGNumberType> values;
  if (!this->parse(&values)) {
    return false;
  }

  radius->fX = values[0];
  radius->fY = values.size() > 1 ? values[1] : values[0];
  return true;
}
}  // namespace tgfx
