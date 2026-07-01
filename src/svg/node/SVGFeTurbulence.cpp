/*
 * Copyright 2020 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tgfx/svg/node/SVGFeTurbulence.h"
#include "svg/SVGAttributeParser.h"
#include "svg/SVGFilterContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

bool SVGFeTurbulence::parseAndSetAttribute(const std::string& name, const std::string& value) {
  return INHERITED::parseAndSetAttribute(name, value) ||
         this->setNumOctaves(
             SVGAttributeParser::parse<SVGIntegerType>("numOctaves", name, value)) ||
         this->setSeed(SVGAttributeParser::parse<SVGNumberType>("seed", name, value)) ||
         this->setBaseFrequency(SVGAttributeParser::parse<SVGFeTurbulenceBaseFrequency>(
             "baseFrequency", name, value)) ||
         this->setTurbulenceType(
             SVGAttributeParser::parse<SVGFeTurbulenceType>("type", name, value)) ||
         this->setStitchTiles(
             SVGAttributeParser::parse<SVGFeStitchTiles>("stitchTiles", name, value));
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

template <>
bool SVGAttributeParser::parse<SVGFeStitchTiles>(SVGFeStitchTiles* stitchTiles) {
  if (this->parseExpectedStringToken("stitch")) {
    *stitchTiles = SVGFeStitchTiles(SVGFeStitchTiles::Type::Stitch);
    return this->parseEOSToken();
  }
  if (this->parseExpectedStringToken("noStitch")) {
    *stitchTiles = SVGFeStitchTiles(SVGFeStitchTiles::Type::NoStitch);
    return this->parseEOSToken();
  }
  return false;
}

std::shared_ptr<ImageFilter> SVGFeTurbulence::onMakeImageFilter(
    const SVGRenderContext&, const SVGFilterContext& filterContext) const {
  auto freq = this->getBaseFrequency();
  float freqX = freq.freqX();
  float freqY = freq.freqY();
  int octaves = this->getNumOctaves();
  float seed = this->getSeed();

  const ISize* tileSizePtr = nullptr;
  ISize tileSizeValue = ISize::MakeEmpty();
  if (this->getStitchTiles().type == SVGFeStitchTiles::Type::Stitch) {
    auto region = filterContext.filterEffectsRegion();
    tileSizeValue =
        ISize::Make(static_cast<int>(region.width()), static_cast<int>(region.height()));
    tileSizePtr = &tileSizeValue;
  }

  std::shared_ptr<Shader> noiseShader;
  if (this->getTurbulenceType().type == SVGFeTurbulenceType::Type::FractalNoise) {
    noiseShader = Shader::MakeFractalNoise(freqX, freqY, octaves, seed, tileSizePtr);
  } else {
    noiseShader = Shader::MakeTurbulence(freqX, freqY, octaves, seed, tileSizePtr);
  }
  if (!noiseShader) {
    return nullptr;
  }
  return ImageFilter::Blend(BlendMode::Src, std::move(noiseShader));
}

}  // namespace tgfx
