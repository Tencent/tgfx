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
#include "layers/NoiseDensityUtils.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/ImageFilter.h"

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

// Draws a noise layer clipped to content alpha. The shader must already have color, density, and
// alpha fully baked in. The shader sampling origin is anchored to contentOffset (expressed in the
// content image's local coordinate space). Callers derive this origin from the layer's surface-space
// position, so the noise pattern stays stable as tiles, dirty regions, or content image sizes
// change. A half-image offset is added to preserve the original "centered" feel of the noise
// relative to the content.
static void DrawNoiseLayer(Canvas* canvas, std::shared_ptr<Image> content,
                           std::shared_ptr<Shader> coloredShader, BlendMode blendMode,
                           const Point& contentOffset, const Path* contentClipPath) {
  if (coloredShader == nullptr || content == nullptr) {
    return;
  }
  auto width = static_cast<float>(content->width());
  auto height = static_cast<float>(content->height());
  auto samplingMatrix = Matrix::MakeTrans(-1.f * contentOffset.x + width * 0.5f,
                                          -1.f * contentOffset.y + height * 0.5f);
  auto centeredShader = coloredShader->makeWithMatrix(samplingMatrix);
  auto blendFilter = ImageFilter::Blend(BlendMode::SrcIn, std::move(centeredShader));
  if (blendFilter == nullptr) {
    return;
  }
  auto noiseImage = content->makeWithFilter(std::move(blendFilter));
  if (noiseImage == nullptr) {
    return;
  }
  bool clipped = false;
  if (contentClipPath != nullptr && canvas->getSurface() == nullptr) {
    canvas->save();
    canvas->clipPath(*contentClipPath);
    clipped = true;
  }
  Paint paint = {};
  paint.setBlendMode(blendMode);
  canvas->drawImage(std::move(noiseImage), &paint);
  if (clipped) {
    canvas->restore();
  }
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
                            const Point& contentOffset, float alpha, BlendMode blendMode,
                            const Path* contentClipPath) {
  if (_density == 0.0f) {
    return;
  }
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
  DrawNoiseLayer(canvas, std::move(content), std::move(coloredShader), blendMode, contentOffset,
                 contentClipPath);
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
                           const Point& contentOffset, float alpha, BlendMode blendMode,
                           const Path* contentClipPath) {
  if (_density == 0.0f) {
    return;
  }
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
      DrawNoiseLayer(canvas, content, std::move(coloredShader), blendMode, contentOffset,
                     contentClipPath);
    }
  }
  {
    auto alphaShader = MakeBrightDensityFilter(noiseShader, _density);
    if (alphaShader != nullptr) {
      float finalAlpha = _secondColor.alpha * alpha;
      Color fillColor = {_secondColor.red, _secondColor.green, _secondColor.blue, finalAlpha};
      auto coloredShader =
          alphaShader->makeWithColorFilter(ColorFilter::Blend(fillColor, BlendMode::SrcIn));
      DrawNoiseLayer(canvas, content, std::move(coloredShader), blendMode, contentOffset,
                     contentClipPath);
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
                             const Point& contentOffset, float alpha, BlendMode blendMode,
                             const Path* contentClipPath) {
  if (_density == 0.0f) {
    return;
  }
  auto noiseShader = getNoiseShader(contentScale);
  if (noiseShader == nullptr) {
    return;
  }
  auto densityShader = MakeDarkDensityFilter(noiseShader, _density);
  if (densityShader == nullptr) {
    return;
  }

  // clang-format off
  std::array<float, 20> contrastMatrix = {
     2.0f, 0.0f, 0.0f, 0.0f, -0.5f,
     0.0f, 2.0f, 0.0f, 0.0f, -0.5f,
     0.0f, 0.0f, 2.0f, 0.0f, -0.5f,
     0.0f, 0.0f, 0.0f, 1.0f,  0.0f,
  };
  // clang-format on
  auto contrastFilter = ColorFilter::Matrix(contrastMatrix);
  auto contrastNoise = noiseShader->makeWithColorFilter(std::move(contrastFilter));
  if (contrastNoise == nullptr) {
    return;
  }
  auto maskedShader =
      Shader::MakeBlend(BlendMode::DstIn, std::move(contrastNoise), std::move(densityShader));
  if (maskedShader == nullptr) {
    return;
  }

  float finalAlpha = _opacity * alpha;
  // clang-format off
  std::array<float, 20> alphaScaleMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f,       0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,       0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,       0.0f,
    0.0f, 0.0f, 0.0f, finalAlpha, 0.0f,
  };
  // clang-format on
  auto alphaScaleFilter = ColorFilter::Matrix(alphaScaleMatrix);
  auto coloredShader = maskedShader->makeWithColorFilter(std::move(alphaScaleFilter));
  DrawNoiseLayer(canvas, std::move(content), std::move(coloredShader), blendMode, contentOffset,
                 contentClipPath);
}

}  // namespace tgfx
