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

#include "tgfx/svg/node/SVGFeLighting.h"
#include "svg/SVGAttributeParser.h"

namespace tgfx {

bool SVGFeLighting::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setSurfaceScale(
             SVGAttributeParser::parse<SVGNumberType>("surfaceScale", name, value)) ||
         this->setUnitLength(SVGAttributeParser::parse<SVGFeLighting::KernelUnitLength>(
             "kernelUnitLength", name, value));
}

template <>
bool SVGAttributeParser::parse<SVGFeLighting::KernelUnitLength>(
    SVGFeLighting::KernelUnitLength* kernelUnitLength) {
  std::vector<SVGNumberType> values;
  if (!this->parse(&values)) {
    return false;
  }

  kernelUnitLength->Dx = values[0];
  kernelUnitLength->Dy = values.size() > 1 ? values[1] : values[0];
  return true;
}

bool SVGFeSpecularLighting::parseAndSetAttribute(const std::string& name,
                                                 const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setSpecularConstant(
             SVGAttributeParser::parse<SVGNumberType>("specularConstant", name, value)) ||
         this->setSpecularExponent(
             SVGAttributeParser::parse<SVGNumberType>("specularExponent", name, value));
}

bool SVGFeDiffuseLighting::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setDiffuseConstant(
             SVGAttributeParser::parse<SVGNumberType>("diffuseConstant", name, value));
}

}  // namespace tgfx
