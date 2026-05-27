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
#include <utility>
#include "core/images/FilterImage.h"
#include "core/utils/Log.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

static int ComputeLinearBucket(float density, float slope, float intercept) {
  density = std::max(0.0f, std::min(1.0f, density));
  auto value = slope * density + intercept;
  auto bucket = static_cast<int>(value + 0.5f);
  return std::max(0, std::min(99, bucket));
}

// Slopes/intercepts are chosen so that at density=0 each band collapses to a single bucket
// (dark=25, bright=74) and at density=1 the bands fully expand without overlap:
//   dark  -> [0, 49], bright -> [50, 99].
// This keeps the transition strictly monotonic across the full 0..1 range, avoiding the
// plateau that occurs when bucket values are clamped at the 0 / 99 boundaries.
static std::pair<int, int> ComputeDarkBandBuckets(float density) {
  auto lower = ComputeLinearBucket(density, -25.0f, 25.0f);
  auto upper = ComputeLinearBucket(density, 24.0f, 25.0f);
  return {lower, upper};
}

static std::pair<int, int> ComputeBrightBandBuckets(float density) {
  auto lower = ComputeLinearBucket(density, -24.0f, 74.0f);
  auto upper = ComputeLinearBucket(density, 25.0f, 74.0f);
  return {lower, upper};
}

static std::shared_ptr<ColorFilter> MakeLumaAlphaThresholdFilter(float threshold) {
  if (threshold <= 0.0f) {
    return nullptr;
  }
  // clang-format off
  std::array<float, 20> lumaMatrix = {
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.0f,    0.0f,    0.0f,    0.0f, 0.0f,
    0.2126f, 0.7152f, 0.0722f, 0.0f, 0.0f,
  };
  // clang-format on
  auto lumaFilter = ColorFilter::Matrix(lumaMatrix);
  auto thresholdFilter = ColorFilter::AlphaThreshold(threshold);
  return ColorFilter::Compose(lumaFilter, thresholdFilter);
}

static std::shared_ptr<Shader> MakeDensityBandShader(std::shared_ptr<Shader> noiseShader,
                                                     int lowerBucket, int upperBucket) {
  if (noiseShader == nullptr || lowerBucket < 0 || upperBucket < lowerBucket) {
    return nullptr;
  }
  auto lowerThreshold = static_cast<float>(std::max(0, std::min(99, lowerBucket))) / 100.0f;
  auto upperThreshold = static_cast<float>(std::max(0, std::min(100, upperBucket + 1))) / 100.0f;
  auto lowerMaskFilter = MakeLumaAlphaThresholdFilter(lowerThreshold);
  auto upperMaskFilter = MakeLumaAlphaThresholdFilter(upperThreshold);
  auto upperMask = noiseShader->makeWithColorFilter(std::move(upperMaskFilter));
  if (lowerMaskFilter == nullptr) {
    return Shader::MakeBlend(BlendMode::DstOut, Shader::MakeColorShader(Color::Black()),
                             std::move(upperMask));
  }
  auto lowerMask = noiseShader->makeWithColorFilter(std::move(lowerMaskFilter));
  return Shader::MakeBlend(BlendMode::DstOut, std::move(lowerMask), std::move(upperMask));
}

static std::shared_ptr<Shader> MakeBrightDensityFilter(std::shared_ptr<Shader> noiseShader,
                                                       float density) {
  auto buckets = ComputeBrightBandBuckets(density);
  return MakeDensityBandShader(std::move(noiseShader), buckets.first, buckets.second);
}

static std::shared_ptr<Shader> MakeDarkDensityFilter(std::shared_ptr<Shader> noiseShader,
                                                     float density) {
  auto buckets = ComputeDarkBandBuckets(density);
  return MakeDensityBandShader(std::move(noiseShader), buckets.first, buckets.second);
}

static std::shared_ptr<Shader> MakeNoiseShader(float size, float scale, float seed) {
  auto freq = 1.0f / (size * scale);
  return Shader::MakeFractalNoise(freq, freq, 3, seed);
}

static std::shared_ptr<ColorFilter> MakeColorFillFilter(const Color& color, float alpha) {
  Color fillColor = {color.red, color.green, color.blue, alpha};
  return ColorFilter::Blend(fillColor, BlendMode::SrcIn);
}

// Shifts the sampling origin of the shader by (shiftX, shiftY) pixels. When the input image is
// clipped, the shift compensates for the offset so the noise pattern remains anchored relative to
// the full content bounds rather than the clipped input image origin.
static std::shared_ptr<Shader> ShiftShader(std::shared_ptr<Shader> shader, float shiftX,
                                           float shiftY) {
  if (shader == nullptr) {
    return nullptr;
  }
  return shader->makeWithMatrix(Matrix::MakeTrans(shiftX, shiftY));
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

void NoiseFilter::onInvalidateFilter() {
  baseDirty = true;
  cachedBaseShader = nullptr;
}

std::shared_ptr<Shader> NoiseFilter::buildAtShift(float scale, const Point& shift) {
  if (baseDirty || cachedScale != scale) {
    cachedBaseShader = onBuildBaseShader(scale);
    cachedScale = scale;
    baseDirty = false;
  }
  if (cachedBaseShader == nullptr) {
    return nullptr;
  }
  return ShiftShader(cachedBaseShader, shift.x, shift.y);
}

std::shared_ptr<Image> NoiseFilter::onFilterImage(std::shared_ptr<Image> input, float scale,
                                                  const Rect& contentBounds, const Rect* clipBounds,
                                                  Point* offset) {
  if (input == nullptr) {
    return nullptr;
  }
  if (_density == 0.0f) {
    return input;
  }
  // Anchor the noise pattern to the content bounds center, expressed in input image pixel space.
  // contentBounds is already in the input image coordinate space (see Layer::mapContentBoundsToImage),
  // so its center is the anchor position directly without an extra scale multiplication.
  Point shift = {contentBounds.centerX(), contentBounds.centerY()};
  auto noiseShader = buildAtShift(scale, shift);
  if (noiseShader == nullptr) {
    return input;
  }
  auto blendFilter = ImageFilter::Blend(BlendMode::SrcIn, std::move(noiseShader));
  if (blendFilter == nullptr) {
    return input;
  }
  return FilterImage::MakeFrom(std::move(input), std::move(blendFilter), offset, clipBounds);
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

std::shared_ptr<Shader> MonoNoiseFilter::onBuildBaseShader(float scale) {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }
  auto darkMask = MakeDarkDensityFilter(noiseShader, _density);
  if (darkMask == nullptr) {
    return nullptr;
  }
  auto colorFilter = MakeColorFillFilter(_color, _color.alpha);
  return darkMask->makeWithColorFilter(std::move(colorFilter));
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

void DuoNoiseFilter::onInvalidateFilter() {
  duoDirty = true;
  cachedDarkBase = nullptr;
  cachedBrightBase = nullptr;
}

std::shared_ptr<Shader> DuoNoiseFilter::onBuildBaseShader(float /*scale*/) {
  // Duo manages its own dual-shader cache in buildAtShift; the base-class single-shader path is
  // unused for Duo.
  DEBUG_ASSERT(false);
  return nullptr;
}

std::shared_ptr<Shader> DuoNoiseFilter::buildAtShift(float scale, const Point& shift) {
  if (duoDirty || cachedDuoScale != scale) {
    auto noiseShader = MakeNoiseShader(_size, scale, _seed);
    if (noiseShader == nullptr) {
      cachedDarkBase = nullptr;
      cachedBrightBase = nullptr;
    } else {
      // Dark noise layer: use low/high limits to form a discrete band, then fill with firstColor.
      auto darkMask = MakeDarkDensityFilter(noiseShader, _density);
      if (darkMask == nullptr) {
        cachedDarkBase = nullptr;
      } else {
        auto firstColorFilter = MakeColorFillFilter(_firstColor, _firstColor.alpha);
        cachedDarkBase = darkMask->makeWithColorFilter(std::move(firstColorFilter));
      }

      // Bright noise layer: use low/high limits to form a discrete band, then fill with
      // secondColor.
      auto brightMask = MakeBrightDensityFilter(noiseShader, _density);
      if (brightMask == nullptr) {
        cachedBrightBase = nullptr;
      } else {
        auto secondColorFilter = MakeColorFillFilter(_secondColor, _secondColor.alpha);
        cachedBrightBase = brightMask->makeWithColorFilter(std::move(secondColorFilter));
      }
    }
    cachedDuoScale = scale;
    duoDirty = false;
  }
  if (cachedDarkBase == nullptr || cachedBrightBase == nullptr) {
    return nullptr;
  }
  auto shiftedDark = ShiftShader(cachedDarkBase, shift.x, shift.y);
  auto shiftedBright = ShiftShader(cachedBrightBase, shift.x, shift.y);
  if (shiftedDark == nullptr || shiftedBright == nullptr) {
    return nullptr;
  }
  return Shader::MakeBlend(BlendMode::SrcOver, std::move(shiftedDark), std::move(shiftedBright));
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

std::shared_ptr<Shader> MultiNoiseFilter::onBuildBaseShader(float scale) {
  auto noiseShader = MakeNoiseShader(_size, scale, _seed);
  if (noiseShader == nullptr) {
    return nullptr;
  }

  // Use dark band bucket for density thresholding (same as MonoNoiseFilter)
  auto darkMask = MakeDarkDensityFilter(noiseShader, _density);
  if (darkMask == nullptr) {
    return nullptr;
  }

  // Contrast enhance the original noise RGB, keep alpha unchanged
  // clang-format off
  std::array<float, 20> contrastMatrix = {
     2.0f,     0.0f,     0.0f,     0.0f, -0.5f,
     0.0f,     2.0f,     0.0f,     0.0f, -0.5f,
     0.0f,     0.0f,     2.0f,     0.0f, -0.5f,
     0.0f,     0.0f,     0.0f,     1.0f,  0.0f,
  };
  // clang-format on
  auto contrastFilter = ColorFilter::Matrix(contrastMatrix);
  auto contrastNoise = noiseShader->makeWithColorFilter(std::move(contrastFilter));
  if (contrastNoise == nullptr) {
    return nullptr;
  }

  // Apply the band mask alpha to the contrast-enhanced noise
  auto masked = Shader::MakeBlend(BlendMode::DstIn, std::move(contrastNoise), std::move(darkMask));
  if (masked == nullptr) {
    return nullptr;
  }

  // Scale alpha by opacity
  // clang-format off
  std::array<float, 20> alphaScaleMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f,     0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,     0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,     0.0f,
    0.0f, 0.0f, 0.0f, _opacity, 0.0f,
  };
  // clang-format on
  auto alphaScaleFilter = ColorFilter::Matrix(alphaScaleMatrix);
  return masked->makeWithColorFilter(std::move(alphaScaleFilter));
}

}  // namespace tgfx
