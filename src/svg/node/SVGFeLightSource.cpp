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

#include "tgfx/svg/node/SVGFeLightSource.h"
#include "svg/SVGAttributeParser.h"

namespace tgfx {

bool SVGFeDistantLight::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setAzimuth(SVGAttributeParser::parse<SVGNumberType>("azimuth", n, v)) ||
         this->setElevation(SVGAttributeParser::parse<SVGNumberType>("elevation", n, v));
}

bool SVGFePointLight::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGNumberType>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGNumberType>("y", n, v)) ||
         this->setZ(SVGAttributeParser::parse<SVGNumberType>("z", n, v));
}

bool SVGFeSpotLight::parseAndSetAttribute(const std::string& n, const std::string& v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGNumberType>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGNumberType>("y", n, v)) ||
         this->setZ(SVGAttributeParser::parse<SVGNumberType>("z", n, v)) ||
         this->setPointsAtX(SVGAttributeParser::parse<SVGNumberType>("pointsAtX", n, v)) ||
         this->setPointsAtY(SVGAttributeParser::parse<SVGNumberType>("pointsAtY", n, v)) ||
         this->setPointsAtZ(SVGAttributeParser::parse<SVGNumberType>("pointsAtZ", n, v)) ||
         this->setSpecularExponent(
             SVGAttributeParser::parse<SVGNumberType>("specularExponent", n, v)) ||
         this->setLimitingConeAngle(
             SVGAttributeParser::parse<SVGNumberType>("limitingConeAngle", n, v));
}
}  // namespace tgfx
