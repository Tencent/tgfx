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

#include "tgfx/svg/node/SVGFeLightSource.h"
#include <cmath>
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

// SkPoint3 SkSVGFeDistantLight::computeDirection() const {
//   // Computing direction from azimuth+elevation is two 3D rotations:
//   //  - Rotate [1,0,0] about y axis first (elevation)
//   //  - Rotate result about z axis (azimuth)
//   // Which is just the first column vector in the 3x3 matrix Rz*Ry.
//   const float azimuthRad = SkDegreesToRadians(fAzimuth);
//   const float elevationRad = SkDegreesToRadians(fElevation);
//   const float sinAzimuth = sinf(azimuthRad), cosAzimuth = cosf(azimuthRad);
//   const float sinElevation = sinf(elevationRad), cosElevation = cosf(elevationRad);
//   return SkPoint3::Make(cosAzimuth * cosElevation, sinAzimuth * cosElevation, sinElevation);
// }

bool SkSVGFeDistantLight::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setAzimuth(SVGAttributeParser::parse<SVGNumberType>("azimuth", n, v)) ||
         this->setElevation(SVGAttributeParser::parse<SVGNumberType>("elevation", n, v));
}

bool SkSVGFePointLight::parseAndSetAttribute(const char* n, const char* v) {
  return INHERITED::parseAndSetAttribute(n, v) ||
         this->setX(SVGAttributeParser::parse<SVGNumberType>("x", n, v)) ||
         this->setY(SVGAttributeParser::parse<SVGNumberType>("y", n, v)) ||
         this->setZ(SVGAttributeParser::parse<SVGNumberType>("z", n, v));
}

bool SkSVGFeSpotLight::parseAndSetAttribute(const char* n, const char* v) {
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