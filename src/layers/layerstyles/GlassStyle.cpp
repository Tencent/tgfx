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

#include "tgfx/layers/layerstyles/GlassStyle.h"
#include <algorithm>
#include <cmath>
#include "GlassRefractionEffect.h"
#include "core/utils/Log.h"

namespace tgfx {

static constexpr float MaxFrostSigma = 50.0f;

std::shared_ptr<GlassStyle> GlassStyle::Make(float refraction, float depth, float frost,
                                             float dispersion, float splay, float lightAngle,
                                             float lightIntensity) {
  return std::shared_ptr<GlassStyle>(
      new GlassStyle(refraction, depth, frost, dispersion, splay, lightAngle, lightIntensity));
}

GlassStyle::GlassStyle(float refraction, float depth, float frost, float dispersion, float splay,
                       float lightAngle, float lightIntensity)
    : _refraction(refraction), _depth(depth), _frost(frost), _dispersion(dispersion), _splay(splay),
      _lightAngle(lightAngle), _lightIntensity(lightIntensity) {
}

void GlassStyle::setRefraction(float value) {
  if (_refraction == value) {
    return;
  }
  _refraction = value;
  invalidateTransform();
}

void GlassStyle::setDepth(float value) {
  if (_depth == value) {
    return;
  }
  _depth = value;
  invalidateTransform();
}

void GlassStyle::setFrost(float value) {
  if (_frost == value) {
    return;
  }
  _frost = value;
  invalidateTransform();
}

void GlassStyle::setDispersion(float value) {
  if (_dispersion == value) {
    return;
  }
  _dispersion = value;
  invalidateTransform();
}

void GlassStyle::setSplay(float value) {
  if (_splay == value) {
    return;
  }
  _splay = value;
  invalidateTransform();
}

void GlassStyle::setLightAngle(float degrees) {
  if (_lightAngle == degrees) {
    return;
  }
  _lightAngle = degrees;
  invalidateTransform();
}

void GlassStyle::setLightIntensity(float value) {
  if (_lightIntensity == value) {
    return;
  }
  _lightIntensity = value;
  invalidateTransform();
}

void GlassStyle::setCornerRadius(float radius) {
  if (_cornerRadius == radius) {
    return;
  }
  _cornerRadius = radius;
  invalidateTransform();
}

void GlassStyle::setShapeType(GlassShapeType type) {
  if (_shapeType == type) {
    return;
  }
  _shapeType = type;
  invalidateTransform();
}

Rect GlassStyle::filterBackground(const Rect& srcRect, float contentScale) {
  auto filter = getFrostFilter(contentScale);
  if (filter) {
    return filter->filterBounds(srcRect);
  }
  // Even without frost, refraction still needs the background content. Return a slightly expanded
  // rect so the Layer system triggers background capture.
  if (_refraction > 0 || _lightIntensity > 0) {
    return srcRect.makeOutset(1.0f, 1.0f);
  }
  return srcRect;
}

void GlassStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float, BlendMode) {
  if (input.extraSource == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }

  auto bgImage = input.extraSource->image();
  auto bgOffset = input.extraSource->imageOffset();

  // Step 1: Frost - apply Gaussian blur to the background
  std::shared_ptr<Image> processedBg = bgImage;
  Point processedOffset = bgOffset;
  if (_frost > 0) {
    auto blurFilter = getFrostFilter(input.contentScale);
    if (blurFilter) {
      Point blurOffset = {};
      auto clipRect = Rect::MakeWH(bgImage->width(), bgImage->height());
      processedBg = bgImage->makeWithFilter(blurFilter, &blurOffset, &clipRect);
      processedOffset += blurOffset;
    }
  }

  // Step 2 & 3: Apply refraction with dispersion and lighting (computed entirely in GPU shader)
  if (_refraction > 0 || _lightIntensity > 0) {
    int layerWidth = input.content->width();
    int layerHeight = input.content->height();

    float halfW = static_cast<float>(layerWidth) * 0.5f;
    float halfH = static_cast<float>(layerHeight) * 0.5f;
    float minHalf = std::min(halfW, halfH);
    float scaledRadius = _cornerRadius * input.contentScale;
    float crRadius = std::min(scaledRadius, minHalf);
    float maxDepth = minHalf - 1.0f;
    float depthPx = (_depth / 100.0f) * maxDepth;
    float depthRatio = std::min(depthPx / maxDepth, 1.0f);
    // refraction 0~100 applies an additional thickness multiplier of 1.0~2.0.
    float refractionFactor = _refraction / 100.0f;
    float thicknessMultiplier = 1.0f + refractionFactor;
    float glassThickness = (1.0f + depthRatio * (minHalf - 1.0f)) * thicknessMultiplier;
    // depth 0~50 maps to flat region 100%~30%, depth 50~100 stays at 30%.
    float flatRatio = (depthRatio <= 0.5f) ? (1.0f - depthRatio * 1.4f) : 0.3f;
    float innerHalfW = halfW * flatRatio;
    float innerHalfH = halfH * flatRatio;
    float innerRadius = crRadius * flatRatio;

    float channelOffset = (_dispersion / 100.0f) * 0.2f;
    float splay = _splay / 100.0f;
    float lightAngle = _lightAngle;
    float lightIntensity = (_lightIntensity > 0) ? _lightIntensity / 100.0f : 0.0f;
    // For AlphaMask shape, generate a UDF height map:
    // 1. Gaussian-blur the binary alpha to approximate UDF
    // 2. Pack the float result into RGBA8 (32-bit precision) via GlassMaskEffect
    std::shared_ptr<Image> maskImage = nullptr;
    if (_shapeType == GlassShapeType::AlphaMask) {
      // Use inner radius for sigma calculation to match actual shape size.
      // For star/polygon shapes, inner radius is much smaller than minHalf.
      float blurSigma = minHalf * (0.2f + (_depth / 100.0f) * 0.2f);
      auto blurFilter = ImageFilter::Blur(blurSigma, blurSigma, TileMode::Decal);
      Point blurMaskOffset = {};
      auto maskClipRect =
          Rect::MakeWH(static_cast<float>(layerWidth), static_cast<float>(layerHeight));
      auto blurredMask =
          input.content->makeWithFilter(blurFilter, &blurMaskOffset, &maskClipRect);
      if (!blurredMask) {
        LOGE("GlassStyle: Failed to blur alpha for UDF, falling back to content.");
        blurredMask = input.content;
      }
      // Pack blurred alpha into RGBA8 32-bit encoding.
      auto packEffect = std::make_shared<GlassMaskEffect>();
      auto packFilter = ImageFilter::Runtime(packEffect);
      Point packOffset = {};
      auto packClipRect = Rect::MakeWH(static_cast<float>(blurredMask->width()),
                                       static_cast<float>(blurredMask->height()));
      maskImage = blurredMask->makeWithFilter(packFilter, &packOffset, &packClipRect);
      if (!maskImage) {
        LOGE("GlassStyle: Failed to pack UDF height map, falling back to blurred.");
        maskImage = blurredMask;
      }
    }
    auto refractionEffect = std::make_shared<GlassRefractionEffect>(
        static_cast<float>(layerWidth), static_cast<float>(layerHeight), halfW, halfH, crRadius,
        minHalf, innerHalfW, innerHalfH, innerRadius, glassThickness, refractionFactor,
        channelOffset, splay, depthRatio, lightAngle, lightIntensity, _shapeType, maskImage);
    auto refractionFilter = ImageFilter::Runtime(refractionEffect);
    Point refractOffset = {};
    auto clipRect = Rect::MakeWH(static_cast<float>(processedBg->width()),
                                 static_cast<float>(processedBg->height()));
    auto refractedImage =
        processedBg->makeWithFilter(std::move(refractionFilter), &refractOffset, &clipRect);
    if (refractedImage) {
      processedBg = refractedImage;
      processedOffset += refractOffset;
    }
  }

  // Mask the processed background with the layer content shape
  auto maskShader = Shader::MakeImageShader(input.content, TileMode::Decal, TileMode::Decal);
  Paint paint = {};
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
  paint.setBlendMode(BlendMode::Src);
  canvas->drawImage(processedBg, processedOffset.x, processedOffset.y, &paint);
}

std::shared_ptr<ImageFilter> GlassStyle::getFrostFilter(float contentScale) {
  if (_frost <= 0) {
    return nullptr;
  }
  if (frostFilter && contentScale == currentFrostScale) {
    return frostFilter;
  }
  currentFrostScale = contentScale;
  float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;
  frostFilter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
  return frostFilter;
}

}  // namespace tgfx
