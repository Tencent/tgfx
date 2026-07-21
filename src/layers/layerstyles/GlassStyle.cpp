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
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "layers/contents/LayerContent.h"
#include "layers/contents/PathContent.h"
#include "layers/contents/RRectContent.h"
#include "layers/contents/RectContent.h"
#include "layers/filters/GlassRefractionImageFilter.h"
#include "layers/filters/TentBlurImageFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

static constexpr float MaxFrostSigma = 50.0f;
// Fallback when the GPU context is unavailable (e.g., picture-only canvas). 4096 is the minimum
// maxTextureDimension2D across all supported platforms (older iOS devices).
static constexpr int MAX_SAFE_TEXTURE_SIZE = 4096;

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
  invalidateRefractionFilter();
}

void GlassStyle::setDepth(float value) {
  if (_depth == value) {
    return;
  }
  _depth = value;
  invalidateRefractionFilter();
  invalidateMaskFilter();
}

void GlassStyle::setFrost(float value) {
  if (_frost == value) {
    return;
  }
  _frost = value;
  invalidateFrostFilter();
}

void GlassStyle::setDispersion(float value) {
  if (_dispersion == value) {
    return;
  }
  _dispersion = value;
  invalidateRefractionFilter();
}

void GlassStyle::setSplay(float value) {
  if (_splay == value) {
    return;
  }
  _splay = value;
  invalidateRefractionFilter();
}

void GlassStyle::setLightAngle(float degrees) {
  if (_lightAngle == degrees) {
    return;
  }
  _lightAngle = degrees;
  invalidateRefractionFilter();
}

void GlassStyle::setLightIntensity(float value) {
  if (_lightIntensity == value) {
    return;
  }
  _lightIntensity = value;
  invalidateRefractionFilter();
}

void GlassStyle::setCornerRadius(float radius) {
  if (_cornerRadius == radius) {
    return;
  }
  _cornerRadius = radius;
  invalidateRefractionFilter();
}

Rect GlassStyle::filterBackground(const Rect& srcRect, float contentScale) {
  auto result = srcRect;
  // Compute frost outset without caching the filter, since onDraw applies frost at a different
  // scale (contentScale * bgScale) to the downsampled background image. Caching here would be
  // invalidated immediately by onDraw.
  if (_frost > 0) {
    float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;
    auto filter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
    result = filter->filterBounds(srcRect);
  }
  // Refraction outset: expand the background capture area to cover the maximum UV offset.
  if (_refraction > 0 || _lightIntensity > 0) {
    auto halfW = srcRect.width() * 0.5f;
    auto halfH = srcRect.height() * 0.5f;
    auto minHalf = std::min(halfW, halfH);
    float refractionFactor = getRefractionFactor();
    float depthRatio = getDepthRatio();
    float glassThickness = getGlassThickness(minHalf);
    float analyticalOffset = glassThickness * refractionFactor;
    // Match the shader's AlphaMask max displacement: halfW * refractionFactor * depthRatio *
    // depthScale, where depthScale = smoothstep(0.0, 0.1, depthRatio).
    float depthT = std::clamp(depthRatio / 0.1f, 0.0f, 1.0f);
    float depthScale = depthT * depthT * (3.0f - 2.0f * depthT);
    float alphaMaskOffset = halfW * refractionFactor * depthRatio * depthScale;
    float refractionOutset = std::max(analyticalOffset, alphaMaskOffset);
    refractionOutset = std::max(refractionOutset, 1.0f);
    result.join(srcRect.makeOutset(refractionOutset, refractionOutset));
  }
  return result;
}

void GlassStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                        BlendMode blendMode) {
  if (input.extraSource == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }

  auto bgImage = input.extraSource->image();
  if (bgImage == nullptr) {
    return;
  }
  auto bgOffset = input.extraSource->imageOffset();

  // Down-scale the background to avoid huge GPU textures when zoomed in.
  // The background image includes outset beyond layer content bounds (for refraction sampling).
  // Use bgImage's actual dimensions to compute scale ratio so coordinates stay aligned.
  auto surface = canvas->getSurface();
  auto context = surface ? surface->getContext() : nullptr;
  auto maxTextureSize =
      context ? context->gpu()->limits()->maxTextureDimension2D : MAX_SAFE_TEXTURE_SIZE;
  Rect origBounds = input.layerContent ? input.layerContent->getBounds() : Rect::MakeWH(0, 0);
  static constexpr float MAX_BG_SIZE = 2048.0f;
  float bgMaxDim = static_cast<float>(std::max(bgImage->width(), bgImage->height()));
  float targetMaxDim =
      std::min(bgMaxDim, std::min(static_cast<float>(maxTextureSize), MAX_BG_SIZE));
  float bgScale = targetMaxDim / bgMaxDim;
  int scaledW =
      std::max(1, static_cast<int>(std::round(static_cast<float>(bgImage->width()) * bgScale)));
  int scaledH =
      std::max(1, static_cast<int>(std::round(static_cast<float>(bgImage->height()) * bgScale)));
  float scaleRatioX = static_cast<float>(scaledW) / static_cast<float>(bgImage->width());
  float scaleRatioY = static_cast<float>(scaledH) / static_cast<float>(bgImage->height());
  bgImage = bgImage->makeScaled(scaledW, scaledH, SamplingOptions(FilterMode::Linear));
  bgOffset.x *= scaleRatioX;
  bgOffset.y *= scaleRatioY;

  // Step 1: Frost - apply Gaussian blur to the background
  std::shared_ptr<Image> processedBg = bgImage;
  Point processedOffset = bgOffset;
  if (_frost > 0) {
    auto blurFilter = getFrostFilter(input.contentScale * bgScale);
    if (blurFilter) {
      Point blurOffset = {};
      auto clipRect = Rect::MakeWH(bgImage->width(), bgImage->height());
      processedBg = bgImage->makeWithFilter(blurFilter, &blurOffset, &clipRect);
      processedOffset += blurOffset;
    }
  }
  // Step 2 & 3: Apply refraction with dispersion and lighting (computed entirely in GPU shader)
  if (_refraction > 0 || _lightIntensity > 0) {
    // layerWidth/Height must match processedBg dimensions for correct UV mapping in shader.
    int layerWidth = processedBg->width();
    int layerHeight = processedBg->height();

    // Use origBounds (not zoom-affected) for all refraction parameters so the effect is
    // zoom-invariant regardless of bgScale downsampling.
    float halfW = origBounds.width() * 0.5f;
    float halfH = origBounds.height() * 0.5f;
    float minHalf = std::min(halfW, halfH);
    float effectiveContentScale = input.contentScale * bgScale;
    float scaledRadius = _cornerRadius * effectiveContentScale;
    float crRadius = std::min(scaledRadius, minHalf);

    // Auto-detect shape type from layerContent.
    GlassShapeType effectiveShapeType = GlassShapeType::AlphaMask;
    if (input.layerContent != nullptr) {
      auto contentType = Types::Get(input.layerContent);
      if (contentType == Types::LayerContentType::RRect) {
        const auto& rRect = static_cast<const RRectContent*>(input.layerContent)->rRect;
        if (rRect.isOval()) {
          effectiveShapeType = GlassShapeType::Ellipse;
        } else {
          effectiveShapeType = GlassShapeType::RoundedRect;
          auto radii = rRect.radii();
          // The analytical SDF currently supports a single scalar corner radius.
          float representativeRadius = std::min(radii[0].x, radii[0].y);
          crRadius = std::min(representativeRadius * effectiveContentScale, minHalf);
        }
      } else if (contentType == Types::LayerContentType::Rect) {
        effectiveShapeType = GlassShapeType::RoundedRect;
        crRadius = 0.0f;
      } else if (contentType == Types::LayerContentType::Path ||
                 contentType == Types::LayerContentType::Shape) {
        // Check if the path is an oval (ellipse).
        if (contentType == Types::LayerContentType::Path) {
          Rect oval = {};
          if (static_cast<const PathContent*>(input.layerContent)->path.isOval(&oval)) {
            effectiveShapeType = GlassShapeType::Ellipse;
          }
        }
      }
    }

    float udfPixelToLayerPixel = 1.0f;
    // For AlphaMask shape, generate a UDF height map:
    // Tent-blur the binary alpha to approximate UDF (triangular kernel gives more linear
    // transition than Gaussian, closer to a true distance field).
    // The maskImage itself is rebuilt every frame (input.content changes), but the
    // mask blur filter is cached for reuse.
    // UDF is capped at MAX_UDF_SIZE to prevent excessive texture allocation. The mask UVs
    // are normalized (0-1), so lower resolution does not affect the refraction shader.
    static constexpr float MAX_UDF_SIZE = 1024.0f;
    std::shared_ptr<Image> maskImage = nullptr;
    std::shared_ptr<Image> coarseMaskImage = nullptr;
    if (effectiveShapeType == GlassShapeType::AlphaMask) {
      if (input.content == nullptr) {
        return;
      }
      // Use original layer bounds (not zoom-affected) for blur radius calculation.
      // depth 1-100 maps linearly to blurRadius 1-50, minimum 5.
      float blurRadius = std::max(std::min((_depth / 100.0f) * 50.0f, 50.0f), 5.0f);
      // UDF texture size is based on the smallest of: original bounds, on-screen pixel size
      // (origBounds * contentScale), and MAX_UDF_SIZE. This avoids allocating a large UDF when
      // the layer is zoomed out — the on-screen pixel count is the effective resolution limit.
      // udfPixelToLayerPixel and blurRadius scale with udfScale, so the shader's refraction
      // distance in layer space remains consistent regardless of UDF resolution.
      float origMaxDim = std::max(origBounds.width(), origBounds.height());
      if (origMaxDim <= 0.0f) {
        return;
      }
      float udfScale = std::min({1.0f, input.contentScale, MAX_UDF_SIZE / origMaxDim});
      int udfWidth = std::max(1, static_cast<int>(std::round(origBounds.width() * udfScale)));
      int udfHeight = std::max(1, static_cast<int>(std::round(origBounds.height() * udfScale)));
      // udfPixelToLayerPixel converts UDF pixel coordinates to layer pixel coordinates in the
      // shader. Both UDF size and layer params are origBounds-based, so the ratio is zoom-invariant.
      udfPixelToLayerPixel = static_cast<float>(origBounds.width()) / static_cast<float>(udfWidth);
      // Scale the content image to the fixed UDF resolution.
      std::shared_ptr<Image> udfContent = input.content;
      if (udfWidth != input.content->width() || udfHeight != input.content->height()) {
        udfContent =
            input.content->makeScaled(udfWidth, udfHeight, SamplingOptions(FilterMode::Linear));
      }
      // Blur radius in UDF pixel space. Since UDF size is origBounds-based, the radius is
      // zoom-invariant.
      float udfBlurRadiusX = blurRadius * udfScale;
      float udfBlurRadiusY = blurRadius * udfScale;
      // depthRatio stays as _depth/100 for shader use (step calculation).
      if (!maskBlurFilter || !FloatNearlyEqual(cachedMaskBlurRadiusX, udfBlurRadiusX) ||
          !FloatNearlyEqual(cachedMaskBlurRadiusY, udfBlurRadiusY)) {
        maskBlurFilter =
            std::make_shared<TentBlurImageFilter>(udfBlurRadiusX, udfBlurRadiusY, TileMode::Decal);
        cachedMaskBlurRadiusX = udfBlurRadiusX;
        cachedMaskBlurRadiusY = udfBlurRadiusY;
      }
      Point blurMaskOffset = {};
      auto maskClipRect = Rect::MakeWH(static_cast<float>(udfWidth), static_cast<float>(udfHeight));
      maskImage = udfContent->makeWithFilter(maskBlurFilter, &blurMaskOffset, &maskClipRect);
      if (!maskImage) {
        LOGE("GlassStyle: Failed to blur alpha for UDF, falling back to content.");
        maskImage = udfContent;
      }

      // Edge light UDF: half the resolution of the fine UDF, fixed small blur radius.
      // Used for edge lighting (edgeWeight) while the fine UDF is used for refraction direction.
      static constexpr float EDGE_LIGHT_BLUR_RADIUS = 5.0f;
      int edgeLightWidth = std::max(1, udfWidth / 2);
      int edgeLightHeight = std::max(1, udfHeight / 2);
      std::shared_ptr<Image> edgeLightContent = udfContent;
      if (edgeLightWidth != udfWidth || edgeLightHeight != udfHeight) {
        edgeLightContent = udfContent->makeScaled(edgeLightWidth, edgeLightHeight,
                                                  SamplingOptions(FilterMode::Linear));
      }
      float edgeLightRadiusX = EDGE_LIGHT_BLUR_RADIUS * udfScale;
      float edgeLightRadiusY = EDGE_LIGHT_BLUR_RADIUS * udfScale;
      if (!coarseMaskBlurFilter ||
          !FloatNearlyEqual(cachedCoarseMaskBlurRadiusX, edgeLightRadiusX) ||
          !FloatNearlyEqual(cachedCoarseMaskBlurRadiusY, edgeLightRadiusY)) {
        coarseMaskBlurFilter = std::make_shared<TentBlurImageFilter>(
            edgeLightRadiusX, edgeLightRadiusY, TileMode::Decal);
        cachedCoarseMaskBlurRadiusX = edgeLightRadiusX;
        cachedCoarseMaskBlurRadiusY = edgeLightRadiusY;
      }
      Point edgeLightBlurOffset = {};
      auto edgeLightClipRect =
          Rect::MakeWH(static_cast<float>(edgeLightWidth), static_cast<float>(edgeLightHeight));
      coarseMaskImage = edgeLightContent->makeWithFilter(coarseMaskBlurFilter, &edgeLightBlurOffset,
                                                         &edgeLightClipRect);
      if (!coarseMaskImage) {
        LOGE("GlassStyle: Failed to blur alpha for edge light UDF, falling back to content.");
        coarseMaskImage = udfContent;
      }
    }
    auto filter = getRefractionFilter(layerWidth, layerHeight, effectiveContentScale,
                                      effectiveShapeType, crRadius, halfW, halfH,
                                      udfPixelToLayerPixel, maskImage, coarseMaskImage);
    Point refractOffset = {};
    auto clipRect = Rect::MakeWH(static_cast<float>(processedBg->width()),
                                 static_cast<float>(processedBg->height()));
    auto refractedImage = processedBg->makeWithFilter(filter, &refractOffset, &clipRect);
    if (refractedImage) {
      processedBg = refractedImage;
      processedOffset += refractOffset;
    }
  }

  // Early exit when neither analytical shape nor content mask is available.
  if (input.layerContent == nullptr && input.content == nullptr) {
    return;
  }

  float finalOffsetX = processedOffset.x / scaleRatioX;
  float finalOffsetY = processedOffset.y / scaleRatioY;
  float finalScaleX = 1.0f / scaleRatioX;
  float finalScaleY = 1.0f / scaleRatioY;

  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);

  // Keep the background and filter chain at their capped resolution. The shader matrix maps the
  // low-resolution image into content pixel space, while the Glass FP executes per destination
  // fragment when the vector shape is drawn.
  auto imageShader = Shader::MakeImageShader(processedBg, TileMode::Decal, TileMode::Decal,
                                             SamplingOptions(FilterMode::Linear));
  auto imageMatrix =
      Matrix::MakeAll(finalScaleX, 0.0f, finalOffsetX, 0.0f, finalScaleY, finalOffsetY);
  imageShader = imageShader->makeWithMatrix(imageMatrix);
  paint.setShader(imageShader);

  bool drewAnalyticalShape = false;
  if (input.layerContent != nullptr) {
    auto contentType = Types::Get(input.layerContent);
    if (contentType == Types::LayerContentType::RRect) {
      auto rRect = static_cast<const RRectContent*>(input.layerContent)->rRect;
      rRect.scale(input.contentScale, input.contentScale);
      canvas->drawRRect(rRect, paint);
      drewAnalyticalShape = true;
    } else if (contentType == Types::LayerContentType::Rect) {
      auto rect = static_cast<const RectContent*>(input.layerContent)->rect;
      rect.scale(input.contentScale, input.contentScale);
      canvas->drawRect(rect, paint);
      drewAnalyticalShape = true;
    } else if (contentType == Types::LayerContentType::Path) {
      Rect oval = {};
      auto& path = static_cast<const PathContent*>(input.layerContent)->path;
      if (path.isOval(&oval)) {
        RRect ovalRRect = {};
        ovalRRect.setOval(oval);
        ovalRRect.scale(input.contentScale, input.contentScale);
        canvas->drawRRect(ovalRRect, paint);
        drewAnalyticalShape = true;
      }
    }
  }

  if (!drewAnalyticalShape) {
    if (input.content == nullptr) {
      return;
    }
    // Keep imageShader on paint so the refraction filter is inlined as a fragment processor
    // (per-destination-fragment computation), matching the SDF path. Using drawImageRect would
    // pass no UV matrix to FilterImage::asFragmentProcessor, causing the containment check to
    // fail and the filter to be pre-rasterized at processedBg resolution, producing seams when
    // zoomed in.
    auto maskShader = Shader::MakeImageShader(input.content, TileMode::Decal, TileMode::Decal);
    paint.setMaskFilter(MaskFilter::MakeShader(maskShader, false));
    auto contentRect = Rect::MakeWH(static_cast<float>(input.content->width()),
                                    static_cast<float>(input.content->height()));
    canvas->drawRect(contentRect, paint);
  }
}

std::shared_ptr<ImageFilter> GlassStyle::getFrostFilter(float contentScale) {
  if (_frost <= 0) {
    return nullptr;
  }
  if (frostFilter && FloatNearlyEqual(currentFrostScale, contentScale)) {
    return frostFilter;
  }
  currentFrostScale = contentScale;
  float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;
  frostFilter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
  return frostFilter;
}

void GlassStyle::invalidateFilter() {
  frostFilter = nullptr;
  refractionFilter = nullptr;
  maskBlurFilter = nullptr;
  coarseMaskBlurFilter = nullptr;
  cachedLayerWidth = 0;
  cachedLayerHeight = 0;
  cachedContentScale = 0.0f;
  cachedCornerRadius = 0.0f;
  cachedMaskBlurRadiusX = 0.0f;
  cachedMaskBlurRadiusY = 0.0f;
  cachedCoarseMaskBlurRadiusX = 0.0f;
  cachedCoarseMaskBlurRadiusY = 0.0f;
  invalidateTransform();
}

void GlassStyle::invalidateRefractionFilter() {
  refractionFilter = nullptr;
  cachedLayerWidth = 0;
  cachedLayerHeight = 0;
  cachedContentScale = 0.0f;
  cachedCornerRadius = 0.0f;
  invalidateTransform();
}

void GlassStyle::invalidateFrostFilter() {
  frostFilter = nullptr;
  invalidateTransform();
}

void GlassStyle::invalidateMaskFilter() {
  maskBlurFilter = nullptr;
  coarseMaskBlurFilter = nullptr;
  cachedMaskBlurRadiusX = 0.0f;
  cachedMaskBlurRadiusY = 0.0f;
  cachedCoarseMaskBlurRadiusX = 0.0f;
  cachedCoarseMaskBlurRadiusY = 0.0f;
  invalidateTransform();
}

std::shared_ptr<ImageFilter> GlassStyle::getRefractionFilter(
    int layerWidth, int layerHeight, float contentScale, GlassShapeType shapeType,
    float cornerRadius, float halfWidth, float halfHeight, float udfPixelToLayerPixel,
    std::shared_ptr<Image> maskImage, std::shared_ptr<Image> coarseMaskImage) {
  // For AlphaMask shapes the mask changes each frame, so always recreate.
  // For analytical shapes we can cache when geometry params haven't changed.
  if (shapeType != GlassShapeType::AlphaMask && refractionFilter &&
      cachedLayerWidth == layerWidth && cachedLayerHeight == layerHeight &&
      cachedContentScale == contentScale && FloatNearlyEqual(cachedCornerRadius, cornerRadius)) {
    return refractionFilter;
  }
  float minHalf = std::min(halfWidth, halfHeight);
  float depthRatio = getDepthRatio();
  float glassThickness = getGlassThickness(minHalf);
  GlassRefractionParams params = {};
  params.glassWidth = static_cast<float>(layerWidth);
  params.glassHeight = static_cast<float>(layerHeight);
  params.halfW = halfWidth;
  params.halfH = halfHeight;
  params.cornerRadius = cornerRadius;
  params.minHalf = minHalf;
  params.glassThickness = glassThickness;
  params.refractionFactor = std::clamp(_refraction / 100.0f, 0.0f, 1.0f);
  // Scale dispersion to [0, 0.2]: the shader offsets R/B UVs by uvOffset * (1 ± dispersion),
  // so 0.2 means max 20% additional offset, keeping chromatic aberration subtle.
  params.dispersion = (_dispersion / 100.0f) * 0.2f;
  params.splay = _splay / 100.0f;
  params.depthRatio = depthRatio;
  params.lightAngle = _lightAngle;
  params.lightIntensity = (_lightIntensity > 0) ? _lightIntensity / 100.0f : 0.0f;
  params.origMinHalf = minHalf;
  params.origWidth = halfWidth * 2.0f;
  params.origHeight = halfHeight * 2.0f;
  params.udfPixelToLayerPixel = udfPixelToLayerPixel;
  params.shapeType = shapeType;
  refractionFilter = std::make_shared<GlassRefractionImageFilter>(params, std::move(maskImage),
                                                                  std::move(coarseMaskImage));
  cachedLayerWidth = layerWidth;
  cachedLayerHeight = layerHeight;
  cachedContentScale = contentScale;
  cachedCornerRadius = cornerRadius;
  return refractionFilter;
}

}  // namespace tgfx
