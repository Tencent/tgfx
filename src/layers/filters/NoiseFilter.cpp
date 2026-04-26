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

// Dark noise mask: Luma -> invert -> threshold. Keeps pixels where luminance < density.
static std::shared_ptr<ColorFilter> MakeDarkDensityFilter(float density) {
  auto lumaFilter = ColorFilter::Luma();
  // clang-format off
  std::array<float, 20> invertAlphaMatrix = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, -1.0f, 1.0f,
  };
  // clang-format on
  auto invertFilter = ColorFilter::Matrix(invertAlphaMatrix);
  auto thresholdFilter = ColorFilter::AlphaThreshold(1.0f - density);
  auto composed = ColorFilter::Compose(lumaFilter, invertFilter);
  return ColorFilter::Compose(composed, thresholdFilter);
}

// Bright noise mask: Luma -> threshold at density. Keeps pixels where luminance >= density.
static std::shared_ptr<ColorFilter> MakeBrightDensityFilter(float density) {
  auto lumaFilter = ColorFilter::Luma();
  auto thresholdFilter = ColorFilter::AlphaThreshold(density);
  return ColorFilter::Compose(lumaFilter, thresholdFilter);
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
  // Clip the blended output by the input image alpha: DstIn keeps only pixels originally opaque.
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
  auto densityFilter = MakeDarkDensityFilter(_density);
  auto colorFilter = MakeColorMatrix(_color, _color.alpha);
  auto composedFilter = ColorFilter::Compose(densityFilter, colorFilter);
  return noiseShader->makeWithColorFilter(std::move(composedFilter));
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
  // Dark layer keeps pixels where luminance < density, filled with firstColor.
  auto darkDensity = MakeDarkDensityFilter(_density);
  auto firstColorFilter = MakeColorMatrix(_firstColor, _firstColor.alpha);
  auto darkComposed = ColorFilter::Compose(darkDensity, firstColorFilter);
  auto darkShader = noiseShader->makeWithColorFilter(std::move(darkComposed));

  // Bright layer keeps pixels where luminance >= density, filled with secondColor.
  auto brightDensity = MakeBrightDensityFilter(_density);
  auto secondColorFilter = MakeColorMatrix(_secondColor, _secondColor.alpha);
  auto brightComposed = ColorFilter::Compose(brightDensity, secondColorFilter);
  auto brightShader = noiseShader->makeWithColorFilter(std::move(brightComposed));

  // Both layers sample the same underlying noise. Thresholds are complementary (< density vs.
  // >= density), so each pixel keeps exactly one of firstColor or secondColor. SrcOver on top of
  // the dark layer yields the dual-color composite in a single shader.
  return Shader::MakeBlend(BlendMode::SrcOver, std::move(darkShader), std::move(brightShader));
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
  // Contrast enhance RGB and write inverted luma to alpha so the subsequent threshold acts on
  // luma-based density.
  // clang-format off
  std::array<float, 20> contrastLumaMatrix = {
     2.0f,     0.0f,     0.0f,     0.0f, -0.5f,
     0.0f,     2.0f,     0.0f,     0.0f, -0.5f,
     0.0f,     0.0f,     2.0f,     0.0f, -0.5f,
    -0.2126f, -0.7152f, -0.0722f,  0.0f,  1.0f,
  };
  // clang-format on
  auto contrastLumaFilter = ColorFilter::Matrix(contrastLumaMatrix);
  auto thresholdFilter = ColorFilter::AlphaThreshold(1.0f - _density);

  // Scale alpha by opacity to control overall visibility.
  // clang-format off
  std::array<float, 20> alphaScaleMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f,     0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,     0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,     0.0f,
    0.0f, 0.0f, 0.0f, _opacity, 0.0f,
  };
  // clang-format on
  auto alphaScaleFilter = ColorFilter::Matrix(alphaScaleMatrix);
  auto composedFilter = ColorFilter::Compose(contrastLumaFilter, thresholdFilter);
  composedFilter = ColorFilter::Compose(composedFilter, alphaScaleFilter);
  return noiseShader->makeWithColorFilter(std::move(composedFilter));
}

}  // namespace tgfx
