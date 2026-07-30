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
#include "layers/CanvasUtils.h"
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
static constexpr float MaxBackgroundSize = 2048.0f;

static float GetRefractionOutset(float width, float height, float refractionFactor,
                                 float depthRatio, float dispersion, float glassThickness) {
  auto minHalf = std::min(width, height) * 0.5f;
  float depthScale = std::clamp(depthRatio / 0.1f, 0.0f, 1.0f);
  depthScale = depthScale * depthScale * (3.0f - 2.0f * depthScale);
  float udfOutset = 0.999f * minHalf * refractionFactor * depthRatio * depthScale;
  float sdfOutset = glassThickness * refractionFactor * 1.2f;
  return std::max(std::max(udfOutset, sdfOutset) * (1.0f + dispersion), 1.0f);
}

static std::shared_ptr<Image> MakeTentBlurImage(Context* context,
                                                const std::shared_ptr<Image>& source, float radiusX,
                                                float radiusY, int paddingX = 0, int paddingY = 0) {
  if (context == nullptr || source == nullptr || radiusX <= 0.0f || radiusY <= 0.0f ||
      paddingX < 0 || paddingY < 0) {
    return nullptr;
  }
  auto textureImage = source->makeTextureImage(context);
  if (textureImage == nullptr) {
    return nullptr;
  }
  auto textureProxy = std::static_pointer_cast<TextureImage>(textureImage)->getTextureProxy();
  SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal, {}, SrcRectConstraint::Fast};
  auto allocator = context->drawingAllocator();
  auto horizontalMatrix = Matrix::MakeTrans(-static_cast<float>(paddingX), 0.0f);
  auto sourceProcessor =
      TiledTextureEffect::Make(allocator, textureProxy, samplingArgs, &horizontalMatrix);
  auto horizontalWidth = source->width() + paddingX * 2;
  auto horizontalTarget =
      RenderTargetProxy::Make(context, horizontalWidth, source->height(), false, 1, false,
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
  auto verticalMatrix = Matrix::MakeTrans(0.0f, -static_cast<float>(paddingY));
  auto verticalSource = TiledTextureEffect::Make(allocator, horizontalTarget->asTextureProxy(),
                                                 samplingArgs, &verticalMatrix);
  auto verticalHeight = source->height() + paddingY * 2;
  auto verticalTarget = RenderTargetProxy::Make(context, horizontalWidth, verticalHeight, false, 1,
                                                false, ImageOrigin::TopLeft, BackingFit::Exact);
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

// Detects whether the layer's vector shape is a regular shape (RoundedRect or Ellipse)
// that can use the analytical SDF path. Only Fill-type shapes are supported; Stroke and
// FillStroke produce a different rendered outline than the fill path, so SDF would mismatch.
static GlassShapeInfo DetectGlassShape(const LayerStyleInput& input) {
  GlassShapeInfo info;
  auto* contourSource = input.findExtraSource(StyleInputSource::Type::Contour);
  if (contourSource == nullptr) {
    return info;
  }
  auto contour = static_cast<const ContourInputSource*>(contourSource);
  if (!contour->shape().has_value()) {
    return info;
  }
  const auto& optShape = contour->shape();
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

Rect GlassStyle::filterBackgroundSoft(const Rect& srcRect, float contentScale) {
  if (_frost <= 0) {
    return srcRect;
  }
  // Do not cache the frost filter: onDraw applies frost at a different scale
  // (contentScale * bgScale) to the downsampled background image, invalidating any cache here.
  float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;
  auto filter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
  auto bounds = filter == nullptr ? srcRect : filter->filterBounds(srcRect);
  return bounds.makeOutset(1.0f, 1.0f);
}

Rect GlassStyle::filterBackgroundSharp(const Rect& srcRect, float contentScale) {
  if (_refraction <= 0 && _lightIntensity <= 0) {
    return _frost > 0 ? srcRect.makeOutset(1.0f, 1.0f) : srcRect;
  }
  float maxWidth = FloatNearlyZero(contentScale) ? srcRect.width() : srcRect.width() / contentScale;
  float maxHeight =
      FloatNearlyZero(contentScale) ? srcRect.height() : srcRect.height() / contentScale;
  // Refraction displacement is defined in the Glass layer's local coordinates. Map the resulting
  // local outset by contentScale instead of deriving it from a transformed AABB, which can
  // underestimate the dependency under non-uniform scaling.
  for (auto owner : owners) {
    if (owner == nullptr) {
      continue;
    }
    auto ownerBounds = owner->getBounds(owner, false);
    maxWidth = std::max(maxWidth, ownerBounds.width());
    maxHeight = std::max(maxHeight, ownerBounds.height());
  }
  // filterBackground() may run before shapeType is determined, so cover both paths.
  auto minHalf = std::min(maxWidth, maxHeight) * 0.5f;
  float refractionOutset =
      GetRefractionOutset(maxWidth, maxHeight, getRefractionFactor(), getDepthRatio(),
                          getDispersionFactor(), getGlassThickness(minHalf));
  refractionOutset = refractionOutset * contentScale + 1.0f;
  if (_frost > 0) {
    float sigma = (_frost / 100.0f) * MaxFrostSigma * contentScale;
    refractionOutset += sigma * 2.0f + 1.0f;
  }
  return srcRect.makeOutset(refractionOutset, refractionOutset);
}

void GlassStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                        BlendMode blendMode) {
  auto* backgroundSource = input.findExtraSource(StyleInputSource::Type::Base);
  if (backgroundSource == nullptr || backgroundSource->image() == nullptr) {
    DEBUG_ASSERT(false);
    return;
  }

  auto bgImage = backgroundSource->image();
  if (input.content == nullptr || FloatNearlyZero(input.contentScale)) {
    return;
  }
  auto bgOffset = backgroundSource->imageOffset();
  auto surface = canvas->getSurface();
  auto context = surface ? surface->getContext() : nullptr;
  if (context == nullptr) {
    return;
  }
  auto maxTextureSize = context->gpu()->limits()->maxTextureDimension2D;
  auto contentWidth = static_cast<float>(input.content->width());
  auto contentHeight = static_cast<float>(input.content->height());
  auto origWidth = contentWidth / input.contentScale;
  auto origHeight = contentHeight / input.contentScale;
  auto origBounds = Rect::MakeWH(origWidth, origHeight);
  if (origBounds.isEmpty()) {
    return;
  }

  float scaleRatioX = 1.0f;
  float scaleRatioY = 1.0f;
  bool usesLocalEvaluation = false;
  Rect visibleRect = {};
  Rect refractInputRect = {};
  auto evaluationLimit = std::min(static_cast<float>(maxTextureSize), MaxBackgroundSize);
  bool backgroundExceedsLimit = static_cast<float>(bgImage->width()) > evaluationLimit ||
                                static_cast<float>(bgImage->height()) > evaluationLimit;
  auto clipBounds = GetCanvasLocalClipBounds(canvas);
  if (backgroundExceedsLimit && clipBounds.has_value()) {
    visibleRect = *clipBounds;
    if (!visibleRect.intersect(Rect::MakeWH(contentWidth, contentHeight))) {
      return;
    }
    visibleRect.roundOut();
    refractInputRect = visibleRect;
    if (_refraction > 0 || _lightIntensity > 0) {
      auto minHalf = std::min(origWidth, origHeight) * 0.5f;
      float refractionOutset =
          GetRefractionOutset(origWidth, origHeight, getRefractionFactor(), getDepthRatio(),
                              getDispersionFactor(), getGlassThickness(minHalf));
      refractionOutset = std::ceil(refractionOutset * input.contentScale + 1.0f);
      refractInputRect.outset(refractionOutset, refractionOutset);
      refractInputRect.roundOut();
    }
    auto backgroundInputRect = refractInputRect;
    auto fullResolutionBlur = getFrostFilter(input.contentScale);
    if (fullResolutionBlur != nullptr) {
      backgroundInputRect = fullResolutionBlur->filterBounds(refractInputRect);
      backgroundInputRect.outset(1.0f, 1.0f);
    }
    backgroundInputRect.roundOut();
    auto availableBackground =
        Rect::MakeXYWH(bgOffset.x, bgOffset.y, static_cast<float>(bgImage->width()),
                       static_cast<float>(bgImage->height()));
    if (backgroundInputRect.intersect(availableBackground)) {
      auto subsetRect = backgroundInputRect;
      subsetRect.offset(-bgOffset.x, -bgOffset.y);
      subsetRect.roundOut();
      if (subsetRect.intersect(Rect::MakeWH(bgImage->width(), bgImage->height()))) {
        if (subsetRect.width() <= evaluationLimit && subsetRect.height() <= evaluationLimit) {
          auto subsetImage = bgImage->makeSubset(subsetRect);
          if (subsetImage != nullptr) {
            bgImage = std::move(subsetImage);
            bgOffset += Point{subsetRect.left, subsetRect.top};
            usesLocalEvaluation = true;
          }
        }
      }
    }
  }

  if (!usesLocalEvaluation) {
    float bgMaxDim = static_cast<float>(std::max(bgImage->width(), bgImage->height()));
    float targetMaxDim =
        std::min(bgMaxDim, std::min(static_cast<float>(maxTextureSize), MaxBackgroundSize));
    float bgScale = targetMaxDim / bgMaxDim;
    int scaledW =
        std::max(1, static_cast<int>(std::round(static_cast<float>(bgImage->width()) * bgScale)));
    int scaledH =
        std::max(1, static_cast<int>(std::round(static_cast<float>(bgImage->height()) * bgScale)));
    scaleRatioX = static_cast<float>(scaledW) / static_cast<float>(bgImage->width());
    scaleRatioY = static_cast<float>(scaledH) / static_cast<float>(bgImage->height());
    bgImage = bgImage->makeScaled(scaledW, scaledH, SamplingOptions(FilterMode::Linear));
    if (bgImage == nullptr) {
      return;
    }
    bgOffset.x *= scaleRatioX;
    bgOffset.y *= scaleRatioY;
  }

  std::shared_ptr<Image> processedBg = bgImage;
  Point processedOffset = bgOffset;
  if (_frost > 0) {
    auto blurFilter = getFrostFilter(input.contentScale * scaleRatioX);
    if (blurFilter != nullptr) {
      Point blurOffset = {};
      auto clipRect = Rect::MakeWH(bgImage->width(), bgImage->height());
      if (usesLocalEvaluation) {
        clipRect = refractInputRect;
        clipRect.offset(-processedOffset.x, -processedOffset.y);
      }
      auto frostedImage = bgImage->makeWithFilter(blurFilter, &blurOffset, &clipRect);
      if (frostedImage != nullptr) {
        processedBg = frostedImage;
        processedOffset += blurOffset;
      }
    }
  }
  // Detect whether the vector shape supports the analytical SDF path.
  auto shapeInfo = DetectGlassShape(input);

  // Step 2 & 3: Apply refraction with dispersion and lighting (computed entirely in GPU shader)
  if (_refraction > 0 || _lightIntensity > 0) {
    // Use origBounds (not zoom-affected) for all refraction parameters so the effect is
    // zoom-invariant regardless of bgScale downsampling.
    float halfW = origBounds.width() * 0.5f;
    float halfH = origBounds.height() * 0.5f;
    float udfPixelToLayerPixelX = 1.0f;
    float udfPixelToLayerPixelY = 1.0f;
    std::shared_ptr<Image> maskImage = nullptr;
    std::shared_ptr<Image> coarseMaskImage = nullptr;
    if (shapeInfo.type == GlassShapeType::AlphaMask) {
      // For AlphaMask shape, generate a UDF height map:
      // Tent-blur the binary alpha to approximate UDF (triangular kernel gives more linear
      // transition than Gaussian, closer to a true distance field).
      // The maskImage is rebuilt every frame via MakeTentBlurImage (input.content changes).
      // The padded UDF is capped at MAX_UDF_SIZE to prevent excessive texture allocation. The
      // mask UVs are normalized (0-1), so lower resolution does not affect the refraction shader.
      static constexpr float MAX_UDF_SIZE = 512.0f;
      static constexpr int UDF_PADDING = 4;
      // Use original layer bounds (not zoom-affected) for blur radius calculation.
      // depth 1-100 maps linearly to blurRadius 5-60. The 60.0 cap is unrelated to the shader's
      // edgeBandWidth cap (same value, different purpose: this limits UDF blur, that limits SDF
      // edge influence zone).
      static constexpr float MAX_BLUR_RADIUS = 60.0f;
      static constexpr float MIN_BLUR_RADIUS = 5.0f;
      float blurRadius =
          std::max(std::min((_depth / 100.0f) * MAX_BLUR_RADIUS, MAX_BLUR_RADIUS), MIN_BLUR_RADIUS);
      // The UDF core follows the larger of the layer-local bounds and the on-screen content
      // bounds, so zooming in produces a finer UDF while zooming out keeps the layer-local
      // resolution. A minimum source size of 128 prevents tiny glass panels from producing an
      // overly coarse UDF. Values are guaranteed > 0 because onDraw rejects empty origBounds and
      // contentScale is non-zero.
      static constexpr float MIN_UDF_SOURCE_SIZE = 128.0f;
      float sourceWidth = std::max(origBounds.width(), contentWidth);
      float sourceHeight = std::max(origBounds.height(), contentHeight);
      float sourceMaxDim = std::max(sourceWidth, sourceHeight);
      float maxPaddedUDFSize = std::min(MAX_UDF_SIZE, static_cast<float>(maxTextureSize));
      float maxCoreUDFSize = maxPaddedUDFSize - UDF_PADDING * 2.0f;
      if (maxCoreUDFSize < 1.0f) {
        return;
      }
      float udfScale =
          std::min({1.0f, maxCoreUDFSize / std::max(sourceMaxDim, MIN_UDF_SOURCE_SIZE)});
      int udfWidth = std::max(1, static_cast<int>(std::round(sourceWidth * udfScale)));
      int udfHeight = std::max(1, static_cast<int>(std::round(sourceHeight * udfScale)));
      // The UDF dimensions are rounded independently, so each axis needs its own layer-pixel
      // conversion ratio. Both values use origBounds so the shader still operates in layer space.
      udfPixelToLayerPixelX = origBounds.width() / static_cast<float>(udfWidth);
      udfPixelToLayerPixelY = origBounds.height() / static_cast<float>(udfHeight);
      // Scale the content image to the fixed UDF resolution.
      std::shared_ptr<Image> udfContent = input.content;
      if (udfWidth != input.content->width() || udfHeight != input.content->height()) {
        udfContent =
            input.content->makeScaled(udfWidth, udfHeight, SamplingOptions(FilterMode::Linear));
        if (udfContent == nullptr) {
          return;
        }
      }
      // Blur radius in UDF pixel space. The radius scales with udfScale relative to the chosen
      // source size, keeping the layer-space radius constant.
      float layerToSourceX = sourceWidth / origBounds.width();
      float layerToSourceY = sourceHeight / origBounds.height();
      float udfBlurRadiusX = blurRadius * udfScale * layerToSourceX;
      float udfBlurRadiusY = blurRadius * udfScale * layerToSourceY;
      // depthRatio stays as _depth/100 for shader use (step calculation). The transparent halo
      // covers the shader's maximum four-texel forward-difference span.
      maskImage = MakeTentBlurImage(context, udfContent, udfBlurRadiusX, udfBlurRadiusY,
                                    UDF_PADDING, UDF_PADDING);
      if (!maskImage) {
        LOGE("GlassStyle: Failed to create padded alpha UDF.");
        return;
      }

      if (_lightIntensity > 0) {
        // Edge light UDF: same resolution as the fine UDF, fixed small blur radius.
        // Used for edge lighting (edgeWeight) while the fine UDF is used for refraction direction.
        static constexpr float EDGE_LIGHT_BLUR_RADIUS = 5.0f;
        std::shared_ptr<Image> edgeLightContent = udfContent;
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
    Point sourceOrigin = {processedOffset.x / scaleRatioX, processedOffset.y / scaleRatioY};
    Point sourcePixelToContentPixel = {1.0f / scaleRatioX, 1.0f / scaleRatioY};
    Point layerPixelToSourcePixel = {input.contentScale * scaleRatioX,
                                     input.contentScale * scaleRatioY};
    auto filter = getRefractionFilter(shapeInfo.type, shapeInfo.cornerRadius, halfW, halfH,
                                      udfPixelToLayerPixelX, udfPixelToLayerPixelY, sourceOrigin,
                                      sourcePixelToContentPixel, layerPixelToSourcePixel,
                                      contentWidth, contentHeight, maskImage, coarseMaskImage);
    Point refractOffset = {};
    auto clipRect = Rect::MakeWH(static_cast<float>(processedBg->width()),
                                 static_cast<float>(processedBg->height()));
    if (usesLocalEvaluation) {
      clipRect = visibleRect;
      clipRect.offset(-sourceOrigin.x, -sourceOrigin.y);
      clipRect.scale(scaleRatioX, scaleRatioY);
    }
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
    // layer's analytical outline. Scale layer-local coordinates to content pixel space, then
    // subtract contentOffset because the content image is cropped to its tight bounds.
    auto drawRRect = shapeInfo.shapeRRect;
    drawRRect.scale(input.contentScale, input.contentScale);
    drawRRect.offset(-input.contentOffset.x, -input.contentOffset.y);
    canvas->drawRRect(drawRRect, paint);
  } else {
    // AlphaMask path: preserve the complete shape while limiting its texture dimensions. Using a
    // visible subset would make the mask depend on the current tile and change its edge semantics.
    auto maskImage = input.content;
    float maskScaleX = 1.0f;
    float maskScaleY = 1.0f;
    auto maskMaxDimension = std::max(maskImage->width(), maskImage->height());
    if (maskMaxDimension > maxTextureSize) {
      float maskScale = static_cast<float>(maxTextureSize) / static_cast<float>(maskMaxDimension);
      int maskWidth = std::max(
          1, static_cast<int>(std::round(static_cast<float>(maskImage->width()) * maskScale)));
      int maskHeight = std::max(
          1, static_cast<int>(std::round(static_cast<float>(maskImage->height()) * maskScale)));
      maskImage = maskImage->makeScaled(maskWidth, maskHeight, SamplingOptions(FilterMode::Linear));
      if (maskImage == nullptr) {
        return;
      }
      maskScaleX = static_cast<float>(input.content->width()) / static_cast<float>(maskWidth);
      maskScaleY = static_cast<float>(input.content->height()) / static_cast<float>(maskHeight);
    }
    auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Decal, TileMode::Decal,
                                              SamplingOptions(FilterMode::Linear));
    maskShader = maskShader->makeWithMatrix(Matrix::MakeScale(maskScaleX, maskScaleY));
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
    GlassShapeType shapeType, float cornerRadius, float halfWidth, float halfHeight,
    float udfPixelToLayerPixelX, float udfPixelToLayerPixelY, const Point& sourceOrigin,
    const Point& sourcePixelToContentPixel, const Point& layerPixelToSourcePixel,
    float contentWidth, float contentHeight, std::shared_ptr<Image> maskImage,
    std::shared_ptr<Image> coarseMaskImage) {
  float minHalf = std::min(halfWidth, halfHeight);
  float depthRatio = getDepthRatio();
  float refractionFactor = getRefractionFactor();
  float splay = std::clamp(_splay / 100.0f, 0.0f, 1.0f);
  GlassRefractionParams params = {};
  params.dispersion = getDispersionFactor();
  params.lightAngle = _lightAngle;
  params.lightIntensity = getLightIntensityFactor();
  params.origWidth = halfWidth * 2.0f;
  params.origHeight = halfHeight * 2.0f;
  params.glassUVScaleX = sourcePixelToContentPixel.x / contentWidth;
  params.glassUVScaleY = sourcePixelToContentPixel.y / contentHeight;
  params.glassUVOffsetX = sourceOrigin.x / contentWidth;
  params.glassUVOffsetY = sourceOrigin.y / contentHeight;
  params.layerPixelToSourcePixelX = layerPixelToSourcePixel.x;
  params.layerPixelToSourcePixelY = layerPixelToSourcePixel.y;
  params.shapeType = shapeType;

  GlassSDFGeometryParams sdfParams = {};
  GlassUDFGeometryParams udfParams = {};
  if (shapeType != GlassShapeType::AlphaMask) {
    sdfParams.halfW = halfWidth;
    sdfParams.halfH = halfHeight;
    sdfParams.cornerRadius = cornerRadius;
    sdfParams.glassThickness = getGlassThickness(minHalf);
    sdfParams.refractionFactor = refractionFactor;
    sdfParams.splay = splay;
    sdfParams.depthRatio = depthRatio;
    params.maxDisplacement = 1.0e20f;
  } else {
    udfParams.halfW = halfWidth;
    udfParams.halfH = halfHeight;
    udfParams.refractionFactor = refractionFactor;
    udfParams.splay = splay;
    udfParams.depthRatio = depthRatio;
    udfParams.udfPixelToLayerPixelX = udfPixelToLayerPixelX;
    udfParams.udfPixelToLayerPixelY = udfPixelToLayerPixelY;
    float depthScale = std::clamp(depthRatio / 0.1f, 0.0f, 1.0f);
    depthScale = depthScale * depthScale * (3.0f - 2.0f * depthScale);
    params.maxDisplacement = 0.999f * minHalf * refractionFactor * depthRatio * depthScale;
  }
  return std::make_shared<GlassRefractionImageFilter>(
      params, sdfParams, udfParams, std::move(maskImage), std::move(coarseMaskImage));
}

}  // namespace tgfx
