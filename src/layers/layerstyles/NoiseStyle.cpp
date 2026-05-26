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

#include "tgfx/layers/layerstyles/NoiseStyle.h"
#include <utility>
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/PictureRecorder.h"

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
  auto lowerMask = noiseShader->makeWithColorFilter(std::move(lowerMaskFilter));
  auto upperMask = noiseShader->makeWithColorFilter(std::move(upperMaskFilter));
  if (lowerMask == nullptr || upperMask == nullptr) {
    return nullptr;
  }
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

// --- NoiseStyle base ---

NoiseStyle::NoiseStyle(float size, float density, float seed)
    : _size(size), _density(density), _seed(seed) {
}

std::shared_ptr<MonoNoiseStyle> NoiseStyle::MakeMono(float size, float density, const Color& color,
                                                     float seed) {
  if (size <= 0) {
    return nullptr;
  }
  return std::shared_ptr<MonoNoiseStyle>(new MonoNoiseStyle(size, density, color, seed));
}

std::shared_ptr<DuoNoiseStyle> NoiseStyle::MakeDuo(float size, float density,
                                                   const Color& firstColor,
                                                   const Color& secondColor, float seed) {
  if (size <= 0) {
    return nullptr;
  }
  return std::shared_ptr<DuoNoiseStyle>(
      new DuoNoiseStyle(size, density, firstColor, secondColor, seed));
}

std::shared_ptr<MultiNoiseStyle> NoiseStyle::MakeMulti(float size, float density, float opacity,
                                                       float seed) {
  if (size <= 0) {
    return nullptr;
  }
  return std::shared_ptr<MultiNoiseStyle>(new MultiNoiseStyle(size, density, opacity, seed));
}

void NoiseStyle::setSize(float size) {
  if (_size == size || size <= 0) {
    return;
  }
  _size = size;
  invalidateNoise();
}

void NoiseStyle::setDensity(float density) {
  density = std::max(0.0f, std::min(1.0f, density));
  if (_density == density) {
    return;
  }
  _density = density;
  invalidateNoise();
}

void NoiseStyle::setSeed(float seed) {
  if (_seed == seed) {
    return;
  }
  _seed = seed;
  invalidateNoise();
}

Rect NoiseStyle::filterBounds(const Rect& srcRect, float) {
  return srcRect;
}

void NoiseStyle::invalidateNoise() {
  invalidateContent();
}

std::shared_ptr<Shader> NoiseStyle::getNoiseShader(float contentScale) const {
  auto freq = 1.0f / (_size * contentScale);
  return Shader::MakeFractalNoise(freq, freq, 3, _seed);
}

// Rasterizes a procedural noise shader into a fixed image at local coordinates (0,0).
// This is needed because drawLayerStyles may use canvas->drawPicture() to replay the
// recording, which would transform shader coordinates by the canvas matrix, causing the
// noise pattern to shift with the layer position.
static std::shared_ptr<Image> RasterizeNoiseShader(std::shared_ptr<Shader> shader, int width,
                                                   int height) {
  PictureRecorder recorder = {};
  auto recordCanvas = recorder.beginRecording();
  Paint paint = {};
  paint.setShader(std::move(shader));
  recordCanvas->drawRect(Rect::MakeWH(static_cast<float>(width), static_cast<float>(height)),
                         paint);
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return nullptr;
  }
  return Image::MakeFrom(std::move(picture), width, height);
}

// Draws a noise layer clipped to content alpha. The shader must already have color, density, and
// alpha fully baked in. The shader sampling origin is anchored to contentOffset (expressed in the
// content image's local coordinate space). Callers derive this origin from the layer's surface-space
// position, so the noise pattern stays stable as tiles, dirty regions, or content image sizes
// change. A half-image offset is added to preserve the original "centered" feel of the noise
// relative to the content.
static void DrawNoiseLayer(Canvas* canvas, std::shared_ptr<Image> content,
                           std::shared_ptr<Shader> coloredShader, BlendMode blendMode,
                           const Point& contentOffset) {
  if (coloredShader == nullptr || content == nullptr) {
    return;
  }
  auto width = content->width();
  auto height = content->height();
  // Shift the procedural noise so that sample (0,0) lands at contentOffset inside the content
  // image. A half-image offset is then added so that the original "centered" appearance is
  // preserved when contentOffset is zero.
  auto samplingMatrix =
      Matrix::MakeTrans(-1.f * contentOffset.x + static_cast<float>(width) * 0.5f,
                        -1.f * contentOffset.y + static_cast<float>(height) * 0.5f);
  auto centeredShader = coloredShader->makeWithMatrix(samplingMatrix);
  auto noiseImage = RasterizeNoiseShader(std::move(centeredShader), width, height);
  if (noiseImage == nullptr) {
    return;
  }
  Paint paint = {};
  paint.setMaskFilter(
      MaskFilter::MakeShader(Shader::MakeImageShader(content, TileMode::Decal, TileMode::Decal)));
  paint.setBlendMode(blendMode);
  canvas->drawImage(std::move(noiseImage), 0, 0, {}, &paint);
}

// --- MonoNoiseStyle ---

MonoNoiseStyle::MonoNoiseStyle(float size, float density, const Color& color, float seed)
    : NoiseStyle(size, density, seed), _color(color) {
}

void MonoNoiseStyle::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateNoise();
}

void MonoNoiseStyle::onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
                            const Point& contentOffset, float alpha, BlendMode blendMode) {
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  auto alphaShader = MakeDarkDensityFilter(noiseShader, _density);
  if (alphaShader == nullptr) {
    return;
  }
  float finalAlpha = _color.alpha * alpha;
  Color fillColor = {_color.red, _color.green, _color.blue, finalAlpha};
  auto coloredShader =
      alphaShader->makeWithColorFilter(ColorFilter::Blend(fillColor, BlendMode::SrcIn));
  DrawNoiseLayer(canvas, std::move(content), std::move(coloredShader), blendMode, contentOffset);
}

// --- DuoNoiseStyle ---

DuoNoiseStyle::DuoNoiseStyle(float size, float density, const Color& firstColor,
                             const Color& secondColor, float seed)
    : NoiseStyle(size, density, seed), _firstColor(firstColor), _secondColor(secondColor) {
}

void DuoNoiseStyle::setFirstColor(const Color& color) {
  if (_firstColor == color) {
    return;
  }
  _firstColor = color;
  invalidateNoise();
}

void DuoNoiseStyle::setSecondColor(const Color& color) {
  if (_secondColor == color) {
    return;
  }
  _secondColor = color;
  invalidateNoise();
}

void DuoNoiseStyle::onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
                           const Point& contentOffset, float alpha, BlendMode blendMode) {
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  {
    auto alphaShader = MakeDarkDensityFilter(noiseShader, _density);
    if (alphaShader != nullptr) {
      float finalAlpha = _firstColor.alpha * alpha;
      Color fillColor = {_firstColor.red, _firstColor.green, _firstColor.blue, finalAlpha};
      auto coloredShader =
          alphaShader->makeWithColorFilter(ColorFilter::Blend(fillColor, BlendMode::SrcIn));
      DrawNoiseLayer(canvas, content, std::move(coloredShader), blendMode, contentOffset);
    }
  }
  {
    auto alphaShader = MakeBrightDensityFilter(noiseShader, _density);
    if (alphaShader != nullptr) {
      float finalAlpha = _secondColor.alpha * alpha;
      Color fillColor = {_secondColor.red, _secondColor.green, _secondColor.blue, finalAlpha};
      auto coloredShader =
          alphaShader->makeWithColorFilter(ColorFilter::Blend(fillColor, BlendMode::SrcIn));
      DrawNoiseLayer(canvas, content, std::move(coloredShader), blendMode, contentOffset);
    }
  }
}

// --- MultiNoiseStyle ---

MultiNoiseStyle::MultiNoiseStyle(float size, float density, float opacity, float seed)
    : NoiseStyle(size, density, seed), _opacity(opacity) {
}

void MultiNoiseStyle::setOpacity(float opacity) {
  opacity = std::max(0.0f, std::min(1.0f, opacity));
  if (_opacity == opacity) {
    return;
  }
  _opacity = opacity;
  invalidateNoise();
}

void MultiNoiseStyle::onDraw(Canvas* canvas, std::shared_ptr<Image> content, float contentScale,
                             const Point& contentOffset, float alpha, BlendMode blendMode) {
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  // Scale alpha by opacity (encode into matrix to avoid paint.setAlpha issues).
  float finalAlpha = _opacity * alpha;
  // clang-format off
  std::array<float, 20> alphaScaleMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, finalAlpha, 0.0f,
  };
  // clang-format on
  auto densityShader = MakeDarkDensityFilter(noiseShader, _density);
  if (densityShader == nullptr) {
    return;
  }
  auto alphaScaleFilter = ColorFilter::Matrix(alphaScaleMatrix);
  auto coloredShader = densityShader->makeWithColorFilter(std::move(alphaScaleFilter));
  DrawNoiseLayer(canvas, std::move(content), std::move(coloredShader), blendMode, contentOffset);
}

}  // namespace tgfx
