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
#include "core/images/FilterImage.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

// Density filter for dark pixels: Luma -> invert -> threshold.
// Keeps pixels where luminance < density (dark noise), discards bright ones.
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

// Complementary density filter for bright pixels: Luma -> threshold at density.
// Keeps pixels where luminance >= density (bright noise), discards dark ones.
static std::shared_ptr<ColorFilter> MakeBrightDensityFilter(float density) {
  auto lumaFilter = ColorFilter::Luma();
  auto thresholdFilter = ColorFilter::AlphaThreshold(density);
  return ColorFilter::Compose(lumaFilter, thresholdFilter);
}

static std::shared_ptr<Shader> MakeNoiseShader(float size, float scale, float seed) {
  auto freq = 1.0f / (size * scale);
  return Shader::MakeFractalNoise(freq, freq, 3, seed);
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

// Shifts the sampling origin of the shader from the image top-left to the image center, so the
// noise pattern is anchored at the input image center and stays stable as the layer bounds change.
static std::shared_ptr<Shader> CenterShader(std::shared_ptr<Shader> shader, int width, int height) {
  if (shader == nullptr) {
    return nullptr;
  }
  return shader->makeWithMatrix(
      Matrix::MakeTrans(static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f));
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

std::shared_ptr<Image> NoiseFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                  Point* offset) {
  if (input == nullptr) {
    return nullptr;
  }
  auto blendFilter = onBuildNoiseImageFilter(scale, input->width(), input->height());
  if (blendFilter == nullptr) {
    return input;
  }
  auto imageShader = Shader::MakeImageShader(input);
  if (imageShader == nullptr) {
    return input;
  }
  auto clipFilter = ImageFilter::Blend(BlendMode::DstIn, std::move(imageShader));
  auto composedFilter = ImageFilter::Compose(std::move(blendFilter), std::move(clipFilter));
  return FilterImage::MakeFrom(std::move(input), std::move(composedFilter), offset);
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

std::shared_ptr<ImageFilter> MonoNoiseFilter::onBuildNoiseImageFilter(float scale, int width,
                                                                      int height) {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }
  auto densityFilter = MakeDarkDensityFilter(_density);
  auto colorFilter = MakeColorMatrix(_color, _color.alpha);
  auto composedFilter = ColorFilter::Compose(densityFilter, colorFilter);
  auto coloredShader = noiseShader->makeWithColorFilter(std::move(composedFilter));
  auto centeredShader = CenterShader(std::move(coloredShader), width, height);
  return ImageFilter::Blend(_blendMode, std::move(centeredShader));
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

std::shared_ptr<ImageFilter> DuoNoiseFilter::onBuildNoiseImageFilter(float scale, int width,
                                                                     int height) {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }

  // Dark noise layer: keeps pixels where luminance < density, filled with firstColor.
  auto darkDensity = MakeDarkDensityFilter(_density);
  auto firstColorFilter = MakeColorMatrix(_firstColor, _firstColor.alpha);
  auto darkComposed = ColorFilter::Compose(darkDensity, firstColorFilter);
  auto darkShader = noiseShader->makeWithColorFilter(std::move(darkComposed));
  auto centeredDark = CenterShader(std::move(darkShader), width, height);
  auto firstFilter = ImageFilter::Blend(_blendMode, std::move(centeredDark));

  // Bright noise layer: keeps pixels where luminance >= density, filled with secondColor.
  auto brightDensity = MakeBrightDensityFilter(_density);
  auto secondColorFilter = MakeColorMatrix(_secondColor, _secondColor.alpha);
  auto brightComposed = ColorFilter::Compose(brightDensity, secondColorFilter);
  auto brightShader = noiseShader->makeWithColorFilter(std::move(brightComposed));
  auto centeredBright = CenterShader(std::move(brightShader), width, height);
  auto secondFilter = ImageFilter::Blend(_blendMode, std::move(centeredBright));

  return ImageFilter::Compose(std::move(firstFilter), std::move(secondFilter));
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

std::shared_ptr<ImageFilter> MultiNoiseFilter::onBuildNoiseImageFilter(float scale, int width,
                                                                       int height) {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }

  // Contrast enhance RGB + inverted luma to alpha for density thresholding.
  // clang-format off
  std::array<float, 20> contrastLumaMatrix = {
     2.0f,    0.0f,    0.0f,   0.0f, -0.5f,
     0.0f,    2.0f,    0.0f,   0.0f, -0.5f,
     0.0f,    0.0f,    2.0f,   0.0f, -0.5f,
    -0.2126f, -0.7152f, -0.0722f, 0.0f, 1.0f,
  };
  // clang-format on
  auto contrastLumaFilter = ColorFilter::Matrix(contrastLumaMatrix);
  auto thresholdFilter = ColorFilter::AlphaThreshold(1.0f - _density);

  // Scale alpha by opacity.
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
  auto coloredShader = noiseShader->makeWithColorFilter(std::move(composedFilter));
  auto centeredShader = CenterShader(std::move(coloredShader), width, height);

  return ImageFilter::Blend(_blendMode, std::move(centeredShader));
}

}  // namespace tgfx
