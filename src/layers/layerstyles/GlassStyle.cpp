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
#include "GlassDisplacementMap.h"
#include "GlassHighlightMap.h"
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

  // Step 2 & 3: Generate displacement map and apply refraction with dispersion
  if (_refraction > 0) {
    int layerWidth = input.content->width();
    int layerHeight = input.content->height();

    float ior = 1.0f + (_refraction / 100.0f) * 4.0f;
    float halfW = static_cast<float>(layerWidth) * 0.5f;
    float halfH = static_cast<float>(layerHeight) * 0.5f;
    float maxThickness = std::min(halfW, halfH);
    float depthPx = (_depth / 100.0f) * maxThickness;
    float scaledRadius = _cornerRadius * input.contentScale;

    auto dispMap = getCachedDisplacementMap(layerWidth, layerHeight, scaledRadius, depthPx, ior);
    if (dispMap) {
      float channelOffset = (_dispersion / 100.0f) * 0.15f;
      // displacementScale must match the encoding normalization in GlassDisplacementMap.
      // Replicate the same maxDisp formula: single elliptical arc over maxInterior.
      float sagitta = std::min(depthPx, maxThickness);
      float edgeThickness = std::max(sagitta * 0.3f, 10.0f);
      edgeThickness = std::min(edgeThickness, sagitta);
      float maxInterior = std::min(halfW, halfH);
      float heightRange = sagitta - edgeThickness;
      float tMin = std::min(1.0f / maxInterior, 0.99f);
      float oneMinusTMin = 1.0f - tMin;
      float denom = std::sqrt(1.0f - oneMinusTMin * oneMinusTMin);
      float maxSlope =
          (denom > 0.001f) ? (heightRange / maxInterior) * oneMinusTMin / denom : 100.0f;
      float nLen = std::sqrt(maxSlope * maxSlope + 1.0f);
      float maxSinI = maxSlope / nLen;
      float maxSinR = std::min(maxSinI / ior, 0.99f);
      float maxCosI = 1.0f / nLen;
      float maxCosR = std::sqrt(1.0f - maxSinR * maxSinR);
      float maxTanI = maxSinI / std::max(maxCosI, 0.001f);
      float maxTanR = maxSinR / std::max(maxCosR, 0.001f);
      float displacementScale = sagitta * std::abs(maxTanI - maxTanR) * 1.5f;
      if (displacementScale < 1.0f) {
        displacementScale = 1.0f;
      }
      fprintf(stderr,
              "[GlassStyle] ior=%.2f sagitta=%.1f edgeH=%.1f maxInt=%.1f "
              "maxSlope=%.3f dispScale=%.3f\n",
              ior, sagitta, edgeThickness, maxInterior, maxSlope, displacementScale);
      auto refractionEffect = std::make_shared<GlassRefractionEffect>(
          dispMap, displacementScale, channelOffset, static_cast<float>(layerWidth),
          static_cast<float>(layerHeight));
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
  }

  // Mask the processed background with the layer content shape
  auto maskShader = Shader::MakeImageShader(input.content, TileMode::Decal, TileMode::Decal);
  Paint paint = {};
  paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
  paint.setBlendMode(BlendMode::Src);
  canvas->drawImage(processedBg, processedOffset.x, processedOffset.y, &paint);

  // Step 4: Generate and composite highlight/shadow maps
  if (_lightIntensity > 0) {
    int layerWidth = input.content->width();
    int layerHeight = input.content->height();
    float maxThicknessHL = std::min(static_cast<float>(layerWidth), static_cast<float>(layerHeight)) * 0.25f;
    float depthPxHL = (_depth / 100.0f) * maxThicknessHL;
    float scaledRadius = _cornerRadius * input.contentScale;
    float highlightOpacity = _lightIntensity / 100.0f;

    auto lightMaps = GlassHighlightMap::Generate(layerWidth, layerHeight, scaledRadius, depthPxHL,
                                                 _splay, _lightAngle, highlightOpacity);

    // Draw specular highlight (white, alpha varies, SrcOver blend)
    if (lightMaps.highlight) {
      Paint highlightPaint = {};
      highlightPaint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
      highlightPaint.setBlendMode(BlendMode::SrcOver);
      canvas->drawImage(lightMaps.highlight, input.contentOffset.x, input.contentOffset.y,
                        &highlightPaint);
    }

    // Draw inner shadow (directional dark/light edges, SrcOver blend)
    if (lightMaps.innerShadow) {
      Paint shadowPaint = {};
      shadowPaint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
      shadowPaint.setBlendMode(BlendMode::SrcOver);
      canvas->drawImage(lightMaps.innerShadow, input.contentOffset.x, input.contentOffset.y,
                        &shadowPaint);
    }
  }
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

std::shared_ptr<Image> GlassStyle::getCachedDisplacementMap(int width, int height,
                                                            float scaledRadius, float depthPx,
                                                            float ior) {
  if (cachedDispMap && cachedDispWidth == width && cachedDispHeight == height &&
      cachedDispRadius == scaledRadius && cachedDispDepth == depthPx && cachedDispIOR == ior) {
    return cachedDispMap;
  }
  cachedDispMap = GlassDisplacementMap::Generate(width, height, scaledRadius, depthPx, ior);
  cachedDispWidth = width;
  cachedDispHeight = height;
  cachedDispRadius = scaledRadius;
  cachedDispDepth = depthPx;
  cachedDispIOR = ior;
  return cachedDispMap;
}

}  // namespace tgfx
