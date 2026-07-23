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
#include "core/images/TextureImage.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "layers/imagefilters/GlassRefractionImageFilter.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "layers/processors/TentBlur1DFragmentProcessor.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

static constexpr float MaxFrostSigma = 50.0f;
static constexpr int MaxTentRadius = 64;

static std::shared_ptr<Image> MakeTentBlurImage(Context* context,
                                                const std::shared_ptr<Image>& source, float radiusX,
                                                float radiusY) {
  if (context == nullptr || source == nullptr || radiusX <= 0.0f || radiusY <= 0.0f) {
    return nullptr;
  }
  auto textureImage = source->makeTextureImage(context);
  if (textureImage == nullptr) {
    return nullptr;
  }
  auto textureProxy = std::static_pointer_cast<TextureImage>(textureImage)->getTextureProxy();
  SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  auto allocator = context->drawingAllocator();
  auto sourceProcessor = TiledTextureEffect::Make(allocator, textureProxy, samplingArgs);
  auto horizontalTarget =
      RenderTargetProxy::Make(context, source->width(), source->height(), false, 1, false,
                              ImageOrigin::TopLeft, BackingFit::Exact);
  if (sourceProcessor == nullptr || horizontalTarget == nullptr) {
    return nullptr;
  }
  auto horizontalProcessor =
      TentBlur1DFragmentProcessor::Make(allocator, std::move(sourceProcessor), radiusX,
                                        TentBlurDirection::Horizontal, 1.0f, MaxTentRadius, false);
  if (!context->drawingManager()->fillRTWithFP(horizontalTarget, std::move(horizontalProcessor),
                                               0)) {
    return nullptr;
  }
  auto verticalSource =
      TiledTextureEffect::Make(allocator, horizontalTarget->asTextureProxy(), samplingArgs);
  auto verticalTarget = RenderTargetProxy::Make(context, source->width(), source->height(), false,
                                                1, false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (verticalSource == nullptr || verticalTarget == nullptr) {
    return nullptr;
  }
  auto verticalProcessor =
      TentBlur1DFragmentProcessor::Make(allocator, std::move(verticalSource), radiusY,
                                        TentBlurDirection::Vertical, 1.0f, MaxTentRadius, true);
  if (!context->drawingManager()->fillRTWithFP(verticalTarget, std::move(verticalProcessor), 0)) {
    return nullptr;
  }
  return TextureImage::Wrap(verticalTarget->asTextureProxy(), nullptr);
}

struct GlassShapeInfo {
  GlassShapeType type = GlassShapeType::AlphaMask;
  float cornerRadius = 0.0f;
  RRect shapeRRect = {};
};

// Detects whether the layer's contour shape is a regular shape (RoundedRect or Ellipse)
// that can use the analytical SDF path. Only Fill-type shapes are supported; Stroke and
// FillStroke produce a different rendered outline than the fill path, so SDF would mismatch.
static GlassShapeInfo DetectGlassShape(const LayerStyleInput& input) {
  GlassShapeInfo info;
  if (input.contourSource == nullptr) {
    return info;
  }
  const auto& optShape = input.contourSource->shape();
  if (!optShape.has_value()) {
    return info;
  }
  if (optShape->type != StyledShapeType::Fill) {
    return info;
  }
  if (optShape->shape == nullptr) {
    return info;
  }
  auto path = optShape->shape->getPath();
  RRect rRect = {};
  Rect rect = {};
  if (path.isRRect(&rRect)) {
    info.shapeRRect = rRect;
    if (rRect.isOval()) {
      info.type = GlassShapeType::Ellipse;
    } else {
      // Shader SDF assumes a single uniform circular radius (rx == ry) across all four corners.
      // Non-uniform or elliptical corners fall back to AlphaMask for accuracy.
      auto radii = rRect.radii();
      bool uniformCircular = radii[0] == radii[1] && radii[1] == radii[2] && radii[2] == radii[3] &&
                             radii[0].x == radii[0].y;
      if (uniformCircular) {
        info.type = GlassShapeType::RoundedRect;
        info.cornerRadius = radii[0].x;
      }
    }
  } else if (path.isOval(&rect)) {
    info.type = GlassShapeType::Ellipse;
    info.shapeRRect = RRect::MakeOval(rect);
  } else if (path.isRect(&rect)) {
    info.type = GlassShapeType::RoundedRect;
    info.cornerRadius = 0.0f;
    info.shapeRRect = RRect::MakeRect(rect);
  }
  return info;
}

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
  invalidateFrostFilter();
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

Rect GlassStyle::filterBackground(const Rect& srcRect, float contentScale) {
  auto result = srcRect;
  auto sizeBounds = srcRect;
  // Layer::updateRenderBounds passes an empty rectangle and reads right/bottom as pure outset
  // amounts. Refraction depends on layer dimensions, so use the owner's bounds in root coordinates
  // for that call. Non-empty rectangles come from dirty-region propagation and must retain their
  // original position.
  if (sizeBounds.isEmpty() && !owners.empty() && owners[0] != nullptr) {
    auto owner = owners[0];
    sizeBounds = owner->getBounds(owner->root(), false);
  }
  // Do not cache the frost filter: onDraw applies frost at a different scale
  // (contentScale * bgScale) to the downsampled background image, invalidating any cache here.
  if (_frost > 0) {
    float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;
    auto filter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
    result = filter->filterBounds(srcRect);
  }
  // Refraction outset: cover the shader's clamped displacement for the most offset chromatic
  // channel. The shader clamps each displacement component to 0.999 * refractionDistance before
  // multiplying R/B offsets by (1 +/- dispersion).
  if (_refraction > 0 || _lightIntensity > 0) {
    auto halfW = sizeBounds.width() * 0.5f;
    auto halfH = sizeBounds.height() * 0.5f;
    auto minHalf = std::min(halfW, halfH);
    float refractionFactor = getRefractionFactor();
    float depthRatio = getDepthRatio();
    float depthT = std::clamp(depthRatio / 0.1f, 0.0f, 1.0f);
    float depthScale = depthT * depthT * (3.0f - 2.0f * depthT);
    float refractionDistance = minHalf * refractionFactor * depthRatio * depthScale;
    float dispersion = (_dispersion / 100.0f) * 0.2f;
    float alphaMaskOutset = 0.999f * refractionDistance * (1.0f + dispersion);
    // SDF analytical outset: glassThickness * refractionFactor covers the maximum displacement
    // used by the SDF shader path (edgeFactor peaks at 1.0 near the shape edge).
    float glassThickness = getGlassThickness(minHalf);
    float analyticalOutset = glassThickness * refractionFactor * (1.0f + dispersion);
    // filterBackground may run before shapeType is determined, so cover both paths conservatively.
    float refractionOutset = std::max(alphaMaskOutset, analyticalOutset);
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
  if (bgImage == nullptr || input.content == nullptr || FloatNearlyZero(input.contentScale)) {
    return;
  }
  auto bgOffset = input.extraSource->imageOffset();

  // Down-scale the background to avoid huge GPU textures when zoomed in.
  // The background image includes outset beyond layer content bounds (for refraction sampling).
  // Use bgImage's actual dimensions to compute scale ratio so coordinates stay aligned.
  auto surface = canvas->getSurface();
  auto context = surface ? surface->getContext() : nullptr;
  if (context == nullptr) {
    return;
  }
  auto maxTextureSize = context->gpu()->limits()->maxTextureDimension2D;
  auto origWidth = static_cast<float>(input.content->width()) / input.contentScale;
  auto origHeight = static_cast<float>(input.content->height()) / input.contentScale;
  auto origBounds = Rect::MakeWH(origWidth, origHeight);
  if (origBounds.isEmpty()) {
    return;
  }
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
  if (bgImage == nullptr) {
    return;
  }
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
  // Detect whether the contour shape supports the analytical SDF path.
  auto shapeInfo = DetectGlassShape(input);

  // Step 2 & 3: Apply refraction with dispersion and lighting (computed entirely in GPU shader)
  if (_refraction > 0 || _lightIntensity > 0) {
    // layerWidth/Height must match processedBg dimensions for correct UV mapping in shader.
    int layerWidth = processedBg->width();
    int layerHeight = processedBg->height();

    // Use origBounds (not zoom-affected) for all refraction parameters so the effect is
    // zoom-invariant regardless of bgScale downsampling.
    float halfW = origBounds.width() * 0.5f;
    float halfH = origBounds.height() * 0.5f;
    float udfPixelToLayerPixel = 1.0f;
    std::shared_ptr<Image> maskImage = nullptr;
    std::shared_ptr<Image> coarseMaskImage = nullptr;
    if (shapeInfo.type == GlassShapeType::AlphaMask) {
      // For AlphaMask shape, generate a UDF height map:
      // Tent-blur the binary alpha to approximate UDF (triangular kernel gives more linear
      // transition than Gaussian, closer to a true distance field).
      // The maskImage is rebuilt every frame via MakeTentBlurImage (input.content changes).
      // UDF is capped at MAX_UDF_SIZE to prevent excessive texture allocation. The mask UVs
      // are normalized (0-1), so lower resolution does not affect the refraction shader.
      static constexpr float MAX_UDF_SIZE = 512.0f;
      // Use original layer bounds (not zoom-affected) for blur radius calculation.
      // depth 1-100 maps linearly to blurRadius 1-60, minimum 5.
      float blurRadius = std::max(std::min((_depth / 100.0f) * 60.0f, 60.0f), 5.0f);
      // UDF texture size follows the original content bounds and is capped at MAX_UDF_SIZE.
      // udfPixelToLayerPixel and blurRadius scale with udfScale, so the shader's refraction
      // distance in layer space remains consistent regardless of UDF resolution.
      float origMaxDim = std::max(origBounds.width(), origBounds.height());
      if (origMaxDim <= 0.0f) {
        return;
      }
      float udfScale = std::min({1.0f, MAX_UDF_SIZE / origMaxDim});
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
        if (udfContent == nullptr) {
          return;
        }
      }
      // Blur radius in UDF pixel space. Since UDF size is origBounds-based, the radius is
      // zoom-invariant.
      float udfBlurRadiusX = blurRadius * udfScale;
      float udfBlurRadiusY = blurRadius * udfScale;
      // depthRatio stays as _depth/100 for shader use (step calculation).
      maskImage = MakeTentBlurImage(context, udfContent, udfBlurRadiusX, udfBlurRadiusY);
      if (!maskImage) {
        LOGE("GlassStyle: Failed to blur alpha for UDF, falling back to content.");
        maskImage = udfContent;
      }

      if (_lightIntensity > 0) {
        // Edge light UDF: half the resolution of the fine UDF, fixed small blur radius.
        // Used for edge lighting (edgeWeight) while the fine UDF is used for refraction direction.
        static constexpr float EDGE_LIGHT_BLUR_RADIUS = 5.0f;
        int edgeLightWidth = std::max(1, udfWidth / 2);
        int edgeLightHeight = std::max(1, udfHeight / 2);
        std::shared_ptr<Image> edgeLightContent = udfContent;
        if (edgeLightWidth != udfWidth || edgeLightHeight != udfHeight) {
          edgeLightContent = udfContent->makeScaled(edgeLightWidth, edgeLightHeight,
                                                    SamplingOptions(FilterMode::Linear));
          if (edgeLightContent == nullptr) {
            return;
          }
        }
        float edgeLightRadiusX = EDGE_LIGHT_BLUR_RADIUS * udfScale;
        float edgeLightRadiusY = EDGE_LIGHT_BLUR_RADIUS * udfScale;
        coarseMaskImage =
            MakeTentBlurImage(context, edgeLightContent, edgeLightRadiusX, edgeLightRadiusY);
        if (!coarseMaskImage) {
          LOGE("GlassStyle: Failed to blur alpha for edge light UDF, falling back to content.");
          coarseMaskImage = edgeLightContent;
        }
      }
    }
    auto filter =
        getRefractionFilter(layerWidth, layerHeight, shapeInfo.type, shapeInfo.cornerRadius, halfW,
                            halfH, udfPixelToLayerPixel, maskImage, coarseMaskImage);
    Point refractOffset = {};
    auto clipRect = Rect::MakeWH(static_cast<float>(processedBg->width()),
                                 static_cast<float>(processedBg->height()));
    auto refractedImage = processedBg->makeWithFilter(filter, &refractOffset, &clipRect);
    if (refractedImage) {
      processedBg = refractedImage;
      processedOffset += refractOffset;
    }
  }

  float finalOffsetX = processedOffset.x / scaleRatioX;
  float finalOffsetY = processedOffset.y / scaleRatioY;
  float finalScaleX = 1.0f / scaleRatioX;
  float finalScaleY = 1.0f / scaleRatioY;

  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);

  // Draw the pre-rasterized refraction result (at capped resolution) into content pixel space.
  auto imageShader = Shader::MakeImageShader(processedBg, TileMode::Decal, TileMode::Decal,
                                             SamplingOptions(FilterMode::Linear));
  auto imageMatrix =
      Matrix::MakeAll(finalScaleX, 0.0f, finalOffsetX, 0.0f, finalScaleY, finalOffsetY);
  imageShader = imageShader->makeWithMatrix(imageMatrix);
  paint.setShader(imageShader);

  if (shapeInfo.type != GlassShapeType::AlphaMask) {
    // SDF path: draw through the vector shape (RRect) so the glass effect is clipped to the
    // layer's analytical outline. The shape is in layer-local coords and must be scaled to
    // content pixel space.
    auto drawRRect = shapeInfo.shapeRRect;
    drawRRect.scale(input.contentScale, input.contentScale);
    canvas->drawRRect(drawRRect, paint);
  } else {
    // AlphaMask path: draw through the content alpha mask so the glass effect is clipped to
    // the layer shape.
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

void GlassStyle::invalidateFrostFilter() {
  frostFilter = nullptr;
  invalidateTransform();
}

std::shared_ptr<ImageFilter> GlassStyle::getRefractionFilter(
    int layerWidth, int layerHeight, GlassShapeType shapeType, float cornerRadius, float halfWidth,
    float halfHeight, float udfPixelToLayerPixel, std::shared_ptr<Image> maskImage,
    std::shared_ptr<Image> coarseMaskImage) {
  float minHalf = std::min(halfWidth, halfHeight);
  float depthRatio = getDepthRatio();
  bool useSDF = (shapeType != GlassShapeType::AlphaMask);
  GlassRefractionParams params = {};
  params.glassWidth = static_cast<float>(layerWidth);
  params.glassHeight = static_cast<float>(layerHeight);
  params.halfW = halfWidth;
  params.halfH = halfHeight;
  params.cornerRadius = cornerRadius;
  params.minHalf = minHalf;
  // glassThickness and origMinHalf are used by the SDF shader path for edge band calculation.
  // For AlphaMask they are set to 0 since the shader does not read them.
  params.glassThickness = useSDF ? getGlassThickness(minHalf) : 0.0f;
  params.refractionFactor = std::clamp(_refraction / 100.0f, 0.0f, 1.0f);
  // Scale dispersion to [0, 0.2]: the shader offsets R/B UVs by uvOffset * (1 ± dispersion),
  // so 0.2 means max 20% additional offset, keeping chromatic aberration subtle.
  params.dispersion = (_dispersion / 100.0f) * 0.2f;
  params.splay = std::clamp(_splay / 100.0f, 0.0f, 1.0f);
  params.depthRatio = depthRatio;
  params.lightAngle = _lightAngle;
  params.lightIntensity = _lightIntensity / 100.0f;
  params.origMinHalf = useSDF ? minHalf : 0.0f;
  params.origWidth = halfWidth * 2.0f;
  params.origHeight = halfHeight * 2.0f;
  params.udfPixelToLayerPixel = udfPixelToLayerPixel;
  params.shapeType = shapeType;
  return std::make_shared<GlassRefractionImageFilter>(params, std::move(maskImage),
                                                      std::move(coarseMaskImage));
}

}  // namespace tgfx
