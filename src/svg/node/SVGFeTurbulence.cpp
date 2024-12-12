/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tgfx/svg/node/SVGFeTurbulence.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/svg/SVGAttributeParser.h"

namespace tgfx {

bool SkSVGFeTurbulence::parseAndSetAttribute(const char* name, const char* value) {
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
    *type = SVGFeTurbulenceType(SVGFeTurbulenceType::FractalNoise);
    parsedValue = true;
  } else if (this->parseExpectedStringToken("turbulence")) {
    *type = SVGFeTurbulenceType(SVGFeTurbulenceType::Turbulence);
    parsedValue = true;
  }

  return parsedValue && this->parseEOSToken();
}

#ifndef RENDER_SVG
std::shared_ptr<ImageFilter> SkSVGFeTurbulence::onMakeImageFilter(
    const SVGRenderContext& /*ctx*/, const SkSVGFilterContext& /*fctx*/) const {
  // const SkISize* tileSize = nullptr;  // TODO: needs filter element subregion properties

  // sk_sp<SkShader> shader;
  // switch (fTurbulenceType.fType) {
  //   case SkSVGFeTurbulenceType::Type::kTurbulence:
  //     shader = SkShaders::MakeTurbulence(fBaseFrequency.freqX(), fBaseFrequency.freqY(),
  //                                        fNumOctaves, fSeed, tileSize);
  //     break;
  //   case SkSVGFeTurbulenceType::Type::kFractalNoise:
  //     shader = SkShaders::MakeFractalNoise(fBaseFrequency.freqX(), fBaseFrequency.freqY(),
  //                                          fNumOctaves, fSeed, tileSize);
  //     break;
  // }

  // return SkImageFilters::Shader(shader, this->resolveFilterSubregion(ctx, fctx));

  //TODO (YG)
  return nullptr;
}
#endif
}  // namespace tgfx