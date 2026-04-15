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
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/PictureRecorder.h"

namespace tgfx {

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

// Density filter for dark pixels: Luma -> invert alpha -> threshold.
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
// alpha fully baked in. The shader is first rasterized into a fixed image so that noise
// coordinates stay local regardless of the canvas matrix during picture playback.
static void DrawNoiseLayer(Canvas* canvas, std::shared_ptr<Image> content,
                           std::shared_ptr<Shader> coloredShader, BlendMode blendMode) {
  if (coloredShader == nullptr || content == nullptr) {
    return;
  }
  auto noiseImage =
      RasterizeNoiseShader(std::move(coloredShader), content->width(), content->height());
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
                            float alpha, BlendMode blendMode) {
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  auto densityFilter = MakeDarkDensityFilter(_density);
  auto alphaShader = noiseShader->makeWithColorFilter(std::move(densityFilter));
  // Encode color RGB and alpha into a ColorFilter matrix.
  // Output: (color.r, color.g, color.b, noise_alpha * color.alpha * layerAlpha)
  float finalAlpha = _color.alpha * alpha;
  // clang-format off
  std::array<float, 20> colorMatrix = {
    0.0f, 0.0f, 0.0f, 0.0f, _color.red,
    0.0f, 0.0f, 0.0f, 0.0f, _color.green,
    0.0f, 0.0f, 0.0f, 0.0f, _color.blue,
    0.0f, 0.0f, 0.0f, finalAlpha, 0.0f,
  };
  // clang-format on
  auto coloredShader = alphaShader->makeWithColorFilter(ColorFilter::Matrix(colorMatrix));
  DrawNoiseLayer(canvas, std::move(content), std::move(coloredShader), blendMode);
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
                           float alpha, BlendMode blendMode) {
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  {
    auto densityFilter = MakeDarkDensityFilter(_density);
    auto alphaShader = noiseShader->makeWithColorFilter(std::move(densityFilter));
    float finalAlpha = _firstColor.alpha * alpha;
    // clang-format off
    std::array<float, 20> colorMatrix = {
      0.0f, 0.0f, 0.0f, 0.0f, _firstColor.red,
      0.0f, 0.0f, 0.0f, 0.0f, _firstColor.green,
      0.0f, 0.0f, 0.0f, 0.0f, _firstColor.blue,
      0.0f, 0.0f, 0.0f, finalAlpha, 0.0f,
    };
    // clang-format on
    auto coloredShader = alphaShader->makeWithColorFilter(ColorFilter::Matrix(colorMatrix));
    DrawNoiseLayer(canvas, content, std::move(coloredShader), blendMode);
  }
  {
    auto densityFilter = MakeBrightDensityFilter(_density);
    auto alphaShader = noiseShader->makeWithColorFilter(std::move(densityFilter));
    float finalAlpha = _secondColor.alpha * alpha;
    // clang-format off
    std::array<float, 20> colorMatrix = {
      0.0f, 0.0f, 0.0f, 0.0f, _secondColor.red,
      0.0f, 0.0f, 0.0f, 0.0f, _secondColor.green,
      0.0f, 0.0f, 0.0f, 0.0f, _secondColor.blue,
      0.0f, 0.0f, 0.0f, finalAlpha, 0.0f,
    };
    // clang-format on
    auto coloredShader = alphaShader->makeWithColorFilter(ColorFilter::Matrix(colorMatrix));
    DrawNoiseLayer(canvas, content, std::move(coloredShader), blendMode);
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
                             float alpha, BlendMode blendMode) {
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  // Contrast enhance RGB + inverted luma to alpha for density thresholding.
  // Since PerlinNoise outputs alpha=1.0, we compute luma from the contrast-enhanced RGB and
  // invert it into alpha for thresholding.
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
  auto alphaScaleFilter = ColorFilter::Matrix(alphaScaleMatrix);
  auto composedFilter = ColorFilter::Compose(contrastLumaFilter, thresholdFilter);
  composedFilter = ColorFilter::Compose(composedFilter, alphaScaleFilter);
  auto coloredShader = noiseShader->makeWithColorFilter(std::move(composedFilter));
  DrawNoiseLayer(canvas, std::move(content), std::move(coloredShader), blendMode);
}

}  // namespace tgfx
