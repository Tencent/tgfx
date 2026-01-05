/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tgfx/svg/node/SVGFeTurbulence.h"
#include "svg/SVGAttributeParser.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

bool SVGFeTurbulence::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setNumOctaves(
             SVGAttributeParser::parse<SVGIntegerType>("numOctaves", name, value)) ||
         this->setSeed(SVGAttributeParser::parse<SVGNumberType>("seed", name, value)) ||
         this->setBaseFrequency(SVGAttributeParser::parse<SVGFeTurbulenceBaseFrequency>(
             "baseFrequency", name, value)) ||
         this->setTurbulenceType(
             SVGAttributeParser::parse<SVGFeTurbulenceType>("type", name, value));
}

template <>
bool SVGAttributeParser::parse<SVGFeTurbulenceBaseFrequency>(SVGFeTurbulenceBaseFrequency* freq) {
  SVGNumberType freqX;
  if (!this->parse(&freqX)) {
    return false;
  }

  SVGNumberType freqY;
  this->parseCommaWspToken();
  if (this->parse(&freqY)) {
    *freq = SVGFeTurbulenceBaseFrequency(freqX, freqY);
  } else {
    *freq = SVGFeTurbulenceBaseFrequency(freqX, freqX);
  }

  return this->parseEOSToken();
}

template <>
bool SVGAttributeParser::parse<SVGFeTurbulenceType>(SVGFeTurbulenceType* type) {
  bool parsedValue = false;

  if (this->parseExpectedStringToken("fractalNoise")) {
    *type = SVGFeTurbulenceType(SVGFeTurbulenceType::Type::FractalNoise);
    parsedValue = true;
  } else if (this->parseExpectedStringToken("turbulence")) {
    *type = SVGFeTurbulenceType(SVGFeTurbulenceType::Type::Turbulence);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

std::shared_ptr<ImageFilter> SVGFeTurbulence::onMakeImageFilter(const SVGRenderContext&,
                                                                const SVGFilterContext&) const {
  //TODO (YGAurora) waiting for turbulence image filter.
  return nullptr;
}

}  // namespace tgfx
