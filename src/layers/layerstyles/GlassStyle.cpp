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
#include "core/utils/MathExtra.h"
#include "layers/contents/LayerContent.h"
#include "tgfx/layers/filters/TentBlurFilter.h"

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
  invalidateFilter();
}

void GlassStyle::setDepth(float value) {
  if (_depth == value) {
    return;
  }
  _depth = value;
  invalidateFilter();
}

void GlassStyle::setFrost(float value) {
  if (_frost == value) {
    return;
  }
  _frost = value;
  invalidateFilter();
}

void GlassStyle::setDispersion(float value) {
  if (_dispersion == value) {
    return;
  }
  _dispersion = value;
  invalidateFilter();
}

void GlassStyle::setSplay(float value) {
  if (_splay == value) {
    return;
  }
  _splay = value;
  invalidateFilter();
}

void GlassStyle::setLightAngle(float degrees) {
  if (_lightAngle == degrees) {
    return;
  }
  _lightAngle = degrees;
  invalidateFilter();
}

void GlassStyle::setLightIntensity(float value) {
  if (_lightIntensity == value) {
    return;
  }
  _lightIntensity = value;
  invalidateFilter();
}

void GlassStyle::setCornerRadius(float radius) {
  if (_cornerRadius == radius) {
    return;
  }
  _cornerRadius = radius;
  invalidateFilter();
}

void GlassStyle::setShapeType(GlassShapeType type) {
  if (_shapeType == type) {
    return;
  }
  _shapeType = type;
  invalidateFilter();
}

Rect GlassStyle::filterBackground(const Rect& srcRect, float contentScale) {
  auto result = srcRect;
  // Frost blur outset (triggers blur-based surface downsampling when large).
  auto filter = getFrostFilter(contentScale);
  if (filter) {
    result = filter->filterBounds(srcRect);
  }
  // Refraction outset: expand the background capture area to cover the maximum UV offset.
  // This increases maxBackgroundOutset (surface expansion) but the caller clamps
  // minBackgroundOutset to the blur budget so refraction does not trigger downsampling.
  if (_refraction > 0 || _lightIntensity > 0) {
    auto halfW = srcRect.width() * 0.5f;
    auto halfH = srcRect.height() * 0.5f;
    auto minHalf = std::min(halfW, halfH);
    float refractionFactor = _refraction / 100.0f;
    float depthRatio = _depth / 100.0f;
    float glassThickness = (1.0f + depthRatio * (minHalf - 1.0f)) * (1.0f + refractionFactor);
    float analyticalOffset = glassThickness * refractionFactor * 2.0f;
    float alphaMaskOffset = halfW * (0.5f * refractionFactor + 0.5f * depthRatio);
    float refractionOutset = std::max(analyticalOffset, alphaMaskOffset);
    refractionOutset = std::max(refractionOutset, 1.0f);
    result.join(srcRect.makeOutset(refractionOutset, refractionOutset));
  }
  return result;
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

    // Auto-detect shape type from layerContent.
    GlassShapeType effectiveShapeType = GlassShapeType::AlphaMask;
    if (input.layerContent != nullptr) {
      auto contentType = input.layerContent->getType();
      if (contentType == LayerContent::Type::RRect) {
        effectiveShapeType = GlassShapeType::RoundedRect;
        auto rrect = input.layerContent->getRRect();
        if (rrect.has_value()) {
          auto radii = rrect->radii();
          // Use the top-left corner radius as representative.
          float maxRadius = std::min(radii[0].x, radii[0].y);
          crRadius = std::min(maxRadius * input.contentScale, minHalf);
        }
      } else if (contentType == LayerContent::Type::Rect) {
        effectiveShapeType = GlassShapeType::RoundedRect;
        crRadius = 0.0f;
      } else if (contentType == LayerContent::Type::Path ||
                 contentType == LayerContent::Type::Shape) {
        // Check if the path is an oval (ellipse).
        if (input.layerContent->getOval().has_value()) {
          effectiveShapeType = GlassShapeType::Ellipse;
        } else {
          effectiveShapeType = GlassShapeType::AlphaMask;
        }
      }
    }

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
    // 1. Tent-blur the binary alpha to approximate UDF (triangular kernel gives more linear
    //    transition than Gaussian, closer to a true distance field)
    // 2. Pack the float result into RGBA8 (32-bit precision) via GlassMaskEffect
    // The maskImage itself is rebuilt every frame (input.content changes), but the
    // GlassMaskEffect pipeline and mask blur filter are cached for reuse.
    std::shared_ptr<Image> maskImage = nullptr;
    if (effectiveShapeType == GlassShapeType::AlphaMask) {
      float blurRadius = minHalf * (_depth / 100.0f) * 0.2f;
      if (!maskBlurFilter || !FloatNearlyEqual(cachedMaskBlurSigma, blurRadius)) {
        maskBlurFilter = TentBlurFilter::MakeImageFilter(blurRadius);
        cachedMaskBlurSigma = blurRadius;
      }
      Point blurMaskOffset = {};
      auto maskClipRect =
          Rect::MakeWH(static_cast<float>(layerWidth), static_cast<float>(layerHeight));
      auto blurredMask =
          input.content->makeWithFilter(maskBlurFilter, &blurMaskOffset, &maskClipRect);
      if (!blurredMask) {
        LOGE("GlassStyle: Failed to blur alpha for UDF, falling back to content.");
        blurredMask = input.content;
      }
      if (!maskEffect) {
        maskEffect = std::make_shared<GlassMaskEffect>();
        maskPackFilter = ImageFilter::Runtime(maskEffect);
      }
      Point packOffset = {};
      auto packClipRect = Rect::MakeWH(static_cast<float>(blurredMask->width()),
                                       static_cast<float>(blurredMask->height()));
      maskImage = blurredMask->makeWithFilter(maskPackFilter, &packOffset, &packClipRect);
      if (!maskImage) {
        LOGE("GlassStyle: Failed to pack UDF height map, falling back to blurred.");
        maskImage = blurredMask;
      }
    }
    auto filter = getRefractionFilter(
        layerWidth, layerHeight, input.contentScale, effectiveShapeType, crRadius, halfW, halfH,
        minHalf, innerHalfW, innerHalfH, innerRadius, glassThickness, refractionFactor,
        channelOffset, splay, depthRatio, lightAngle, lightIntensity);
    // Inject the current frame's maskImage (or empty) into the cached effect via setExtraInputs.
    // extraInputs is read by RuntimeImageFilter::lockTextureProxy during makeWithFilter, so this
    // must be called before makeWithFilter.
    if (effectiveShapeType == GlassShapeType::AlphaMask && maskImage) {
      refractionEffect->setExtraInputs({maskImage});
    } else {
      refractionEffect->setExtraInputs({});
    }
    Point refractOffset = {};
    auto clipRect = Rect::MakeWH(static_cast<float>(processedBg->width()),
                                 static_cast<float>(processedBg->height()));
    auto refractedImage = processedBg->makeWithFilter(filter, &refractOffset, &clipRect);
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

void GlassStyle::invalidateFilter() {
  frostFilter = nullptr;
  refractionEffect = nullptr;
  refractionFilter = nullptr;
  maskEffect = nullptr;
  maskPackFilter = nullptr;
  maskBlurFilter = nullptr;
  cachedLayerWidth = 0;
  cachedLayerHeight = 0;
  cachedContentScale = 0.0f;
  cachedCornerRadius = 0.0f;
  cachedMaskBlurSigma = 0.0f;
  invalidateTransform();
}

std::shared_ptr<ImageFilter> GlassStyle::getRefractionFilter(
    int layerWidth, int layerHeight, float contentScale, GlassShapeType shapeType,
    float cornerRadius, float halfWidth, float halfHeight, float minHalf, float innerHalfWidth,
    float innerHalfHeight, float innerRadius, float glassThickness, float refractionFactor,
    float dispersion, float splay, float depthRatio, float lightAngle, float lightIntensity) {
  if (refractionFilter && cachedLayerWidth == layerWidth && cachedLayerHeight == layerHeight &&
      cachedContentScale == contentScale && cachedShapeType == shapeType &&
      FloatNearlyEqual(cachedCornerRadius, cornerRadius)) {
    return refractionFilter;
  }
  refractionEffect = std::make_shared<GlassRefractionEffect>(
      static_cast<float>(layerWidth), static_cast<float>(layerHeight), halfWidth, halfHeight,
      cornerRadius, minHalf, innerHalfWidth, innerHalfHeight, innerRadius, glassThickness,
      refractionFactor, dispersion, splay, depthRatio, lightAngle, lightIntensity, shapeType);
  refractionFilter = ImageFilter::Runtime(refractionEffect);
  cachedLayerWidth = layerWidth;
  cachedLayerHeight = layerHeight;
  cachedContentScale = contentScale;
  cachedShapeType = shapeType;
  cachedCornerRadius = cornerRadius;
  return refractionFilter;
}

}  // namespace tgfx
