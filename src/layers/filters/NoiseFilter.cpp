/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/layers/filters/NoiseFilter.h"
#include <algorithm>
#include <array>
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

// Alpha band center used by all three noise modes, matching Figma's SVG export where the
// luminanceToAlpha discrete table is always centered at bucket 25 out of 100. This is the empirical
// default from Figma; callers map their UI density parameter to our `density` input which is then
// treated as the band width.
static constexpr float kBandCenter = 0.25f;

// Writes dot(rgb, lumaCoeffs) into the alpha channel (RGB zeroed). Equivalent to SVG's
// feColorMatrix type="luminanceToAlpha". Using Matrix instead of ColorFilter::Luma() avoids
// computing luma on premultiplied RGB, which would scale by the source alpha and produce a biased
// mask.
static std::shared_ptr<ColorFilter> MakeLuminanceToAlphaFilter() {
  // clang-format off
  std::array<float, 20> luminanceToAlphaMatrix = {
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
  };
  // clang-format on
  return ColorFilter::Matrix(luminanceToAlphaMatrix);
}

// Given a shader whose alpha channel is the signal to band-pass, returns a shader whose alpha is 1
// when the input alpha lies in [lo, hi) and 0 otherwise. Implementation: take two alpha-threshold
// mask shaders (at lo and hi) and subtract via Shader::MakeBlend(SrcOut). SrcOut computes
// src * (1 - dst.a), so by setting src = mask(lo) and dst = mask(hi) we get "passes lo but not hi".
static std::shared_ptr<Shader> MakeBandPassShader(std::shared_ptr<Shader> alphaShader, float lo,
                                                  float hi) {
  if (alphaShader == nullptr || hi <= lo) {
    return nullptr;
  }
  auto aboveLo = alphaShader->makeWithColorFilter(ColorFilter::AlphaThreshold(lo));
  auto aboveHi = alphaShader->makeWithColorFilter(ColorFilter::AlphaThreshold(hi));
  if (aboveLo == nullptr || aboveHi == nullptr) {
    return nullptr;
  }
  return Shader::MakeBlend(BlendMode::SrcOut, std::move(aboveHi), std::move(aboveLo));
}

static std::shared_ptr<ColorFilter> MakeColorMatrix(const Color& color, float alpha) {
  // clang-format off
  std::array<float, 20> colorMatrix = {
    0.0f, 0.0f, 0.0f, 0.0f, color.red,
    0.0f, 0.0f, 0.0f, 0.0f, color.green,
    0.0f, 0.0f, 0.0f, 0.0f, color.blue,
    0.0f, 0.0f, 0.0f, alpha, 0.0f,
  };
  // clang-format on
  return ColorFilter::Matrix(colorMatrix);
}

// Builds a fractal Perlin noise shader at the given grain size and random seed, scaled to the
// rasterization resolution.
static std::shared_ptr<Shader> MakeNoiseShader(float size, float scale, float seed) {
  auto freq = 1.0f / (size * scale);
  return Shader::MakeFractalNoise(freq, freq, 3, seed);
}

// --- NoiseFilter base ---

NoiseFilter::NoiseFilter(float size, float density, float seed, BlendMode blendMode)
    : _size(size), _density(std::max(0.0f, std::min(1.0f, density))), _seed(seed),
      _blendMode(blendMode) {
}

std::shared_ptr<MonoNoiseFilter> NoiseFilter::MakeMono(float size, float density,
                                                       const Color& color, float seed,
                                                       BlendMode blendMode) {
  if (size <= 0) {
    return nullptr;
  }
  return std::shared_ptr<MonoNoiseFilter>(
      new MonoNoiseFilter(size, density, color, seed, blendMode));
}

std::shared_ptr<DuoNoiseFilter> NoiseFilter::MakeDuo(float size, float density,
                                                     const Color& firstColor,
                                                     const Color& secondColor, float seed,
                                                     BlendMode blendMode) {
  if (size <= 0) {
    return nullptr;
  }
  return std::shared_ptr<DuoNoiseFilter>(
      new DuoNoiseFilter(size, density, firstColor, secondColor, seed, blendMode));
}

std::shared_ptr<MultiNoiseFilter> NoiseFilter::MakeMulti(float size, float density, float opacity,
                                                         float seed, BlendMode blendMode) {
  if (size <= 0) {
    return nullptr;
  }
  return std::shared_ptr<MultiNoiseFilter>(
      new MultiNoiseFilter(size, density, opacity, seed, blendMode));
}

void NoiseFilter::setSize(float size) {
  if (_size == size || size <= 0) {
    return;
  }
  _size = size;
  invalidateFilter();
}

void NoiseFilter::setDensity(float density) {
  density = std::max(0.0f, std::min(1.0f, density));
  if (_density == density) {
    return;
  }
  _density = density;
  invalidateFilter();
}

void NoiseFilter::setSeed(float seed) {
  if (_seed == seed) {
    return;
  }
  _seed = seed;
  invalidateFilter();
}

void NoiseFilter::setBlendMode(BlendMode blendMode) {
  if (_blendMode == blendMode) {
    return;
  }
  _blendMode = blendMode;
  invalidateFilter();
}

std::shared_ptr<Image> NoiseFilter::onFilterImage(std::shared_ptr<Image> input,
                                                  const Rect& contentBounds, float scale,
                                                  Point* offset) {
  auto noiseShader = onBuildNoiseShader(scale);
  if (noiseShader == nullptr) {
    return input;
  }
  // Translate the shader so its origin lands at the content center inside the input image. The
  // view matrix on the output side uses (-cx, -cy) because makeWithMatrix composes as
  // output -> shader-local, so pixel (cx, cy) maps back to shader origin (0, 0).
  auto centeredShader = noiseShader->makeWithMatrix(
      Matrix::MakeTrans(-contentBounds.centerX(), -contentBounds.centerY()));
  auto blendFilter = ImageFilter::Blend(_blendMode, std::move(centeredShader));
  if (blendFilter == nullptr) {
    return input;
  }
  // Clip the blended output to the input image alpha via Blend(DstIn, imageShader). Composing
  // blendFilter with clipFilter reproduces the BlendImageFilterClipToSource pattern used in
  // FilterTest: noise replaces the source, then DstIn restores the original alpha coverage.
  auto inputShader = Shader::MakeImageShader(input);
  if (inputShader == nullptr) {
    return input;
  }
  auto clipFilter = ImageFilter::Blend(BlendMode::DstIn, std::move(inputShader));
  auto composedFilter = ImageFilter::Compose(std::move(blendFilter), std::move(clipFilter));
  return input->makeWithFilter(std::move(composedFilter), offset);
}

// --- MonoNoiseFilter ---

MonoNoiseFilter::MonoNoiseFilter(float size, float density, const Color& color, float seed,
                                 BlendMode blendMode)
    : NoiseFilter(size, density, seed, blendMode), _color(color) {
}

void MonoNoiseFilter::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

std::shared_ptr<Shader> MonoNoiseFilter::onBuildNoiseShader(float scale) const {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }
  // SVG pipeline for Mono: luminanceToAlpha -> discrete band-pass centered at kBandCenter.
  auto alphaShader = noiseShader->makeWithColorFilter(MakeLuminanceToAlphaFilter());
  float halfWidth = _density * 0.5f;
  auto maskShader =
      MakeBandPassShader(std::move(alphaShader), kBandCenter - halfWidth, kBandCenter + halfWidth);
  if (maskShader == nullptr) {
    return nullptr;
  }
  // Replace RGB with the requested color and scale alpha by color.alpha.
  auto colorFilter = MakeColorMatrix(_color, _color.alpha);
  return maskShader->makeWithColorFilter(std::move(colorFilter));
}

// --- DuoNoiseFilter ---

DuoNoiseFilter::DuoNoiseFilter(float size, float density, const Color& firstColor,
                               const Color& secondColor, float seed, BlendMode blendMode)
    : NoiseFilter(size, density, seed, blendMode), _firstColor(firstColor),
      _secondColor(secondColor) {
}

void DuoNoiseFilter::setFirstColor(const Color& color) {
  if (_firstColor == color) {
    return;
  }
  _firstColor = color;
  invalidateFilter();
}

void DuoNoiseFilter::setSecondColor(const Color& color) {
  if (_secondColor == color) {
    return;
  }
  _secondColor = color;
  invalidateFilter();
}

std::shared_ptr<Shader> DuoNoiseFilter::onBuildNoiseShader(float scale) const {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }
  // SVG pipeline for Duo: luminanceToAlpha -> two mirrored band-passes, one at kBandCenter filled
  // with firstColor, the other at 1 - kBandCenter filled with secondColor, merged with SrcOver.
  auto alphaShader = noiseShader->makeWithColorFilter(MakeLuminanceToAlphaFilter());
  float halfWidth = _density * 0.5f;

  auto firstMask =
      MakeBandPassShader(alphaShader, kBandCenter - halfWidth, kBandCenter + halfWidth);
  auto secondCenter = 1.0f - kBandCenter;
  auto secondMask =
      MakeBandPassShader(alphaShader, secondCenter - halfWidth, secondCenter + halfWidth);
  if (firstMask == nullptr || secondMask == nullptr) {
    return nullptr;
  }
  auto firstShader =
      firstMask->makeWithColorFilter(MakeColorMatrix(_firstColor, _firstColor.alpha));
  auto secondShader =
      secondMask->makeWithColorFilter(MakeColorMatrix(_secondColor, _secondColor.alpha));

  return Shader::MakeBlend(BlendMode::SrcOver, std::move(firstShader), std::move(secondShader));
}

// --- MultiNoiseFilter ---

MultiNoiseFilter::MultiNoiseFilter(float size, float density, float opacity, float seed,
                                   BlendMode blendMode)
    : NoiseFilter(size, density, seed, blendMode),
      _opacity(std::max(0.0f, std::min(1.0f, opacity))) {
}

void MultiNoiseFilter::setOpacity(float opacity) {
  opacity = std::max(0.0f, std::min(1.0f, opacity));
  if (_opacity == opacity) {
    return;
  }
  _opacity = opacity;
  invalidateFilter();
}

std::shared_ptr<Shader> MultiNoiseFilter::onBuildNoiseShader(float scale) const {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }
  // SVG pipeline for Multi:
  //   feFuncR/G/B linear slope=2 intercept=-0.5  (RGB contrast 2x - 0.5, alpha untouched)
  //   feFuncA discrete tableValues=...           (band-pass on noise's own alpha channel)
  //   feFuncA table="0 opacity"                  (scale final alpha by opacity)
  // Steps here translate that to ColorFilter + Shader primitives.

  // Step A: RGB contrast enhancement. Alpha row preserves the fourth noise channel untouched
  // because SVG feTurbulence's alpha is independent of RGB.
  // clang-format off
  std::array<float, 20> contrastMatrix = {
    2.0f, 0.0f, 0.0f, 0.0f, -0.5f,
    0.0f, 2.0f, 0.0f, 0.0f, -0.5f,
    0.0f, 0.0f, 2.0f, 0.0f, -0.5f,
    0.0f, 0.0f, 0.0f, 1.0f,  0.0f,
  };
  // clang-format on
  auto colorShader = noiseShader->makeWithColorFilter(ColorFilter::Matrix(contrastMatrix));
  if (colorShader == nullptr) {
    return nullptr;
  }

  // Step B: build a band-pass mask from the same noise alpha, then SrcIn the mask onto colorShader
  // so the contrast-enhanced RGB only shows through where the mask passes. Using the noise shader
  // directly as the alpha source keeps Multi aligned with Figma (where the discrete table is fed
  // by feTurbulence alpha, not luma).
  float halfWidth = _density * 0.5f;
  auto maskShader =
      MakeBandPassShader(noiseShader, kBandCenter - halfWidth, kBandCenter + halfWidth);
  if (maskShader == nullptr) {
    return nullptr;
  }
  auto bandedColorShader =
      Shader::MakeBlend(BlendMode::SrcIn, std::move(maskShader), std::move(colorShader));
  if (bandedColorShader == nullptr) {
    return nullptr;
  }

  // Step C: scale the final alpha by opacity.
  // clang-format off
  std::array<float, 20> alphaScaleMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f,     0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,     0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,     0.0f,
    0.0f, 0.0f, 0.0f, _opacity, 0.0f,
  };
  // clang-format on
  return bandedColorShader->makeWithColorFilter(ColorFilter::Matrix(alphaScaleMatrix));
}

}  // namespace tgfx
