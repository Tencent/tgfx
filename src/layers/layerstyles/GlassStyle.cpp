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
#include "layers/processors/GlassUDFTentBlurFragmentProcessor.h"
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

// Blurs a window of the coverage image with two tent radii and returns one RGBA8 image. RGB holds
// the refraction field and A holds the edge-light field. Coordinates remain in the full UDF space.
static std::shared_ptr<Image> MakeGlassUDFImage(Context* context,
                                                const std::shared_ptr<Image>& source,
                                                int coreWidth, int coreHeight,
                                                const Rect& textureRect,
                                                const Point& fineRadius,
                                                const Point& coarseRadius) {
  if (context == nullptr || source == nullptr || coreWidth <= 0 || coreHeight <= 0 ||
      textureRect.isEmpty()) {
    return nullptr;
  }
  if (fineRadius.x <= 0.0f || fineRadius.y <= 0.0f || coarseRadius.x <= 0.0f ||
      coarseRadius.y <= 0.0f) {
    return nullptr;
  }
  auto textureWidth = static_cast<int>(std::round(textureRect.width()));
  auto textureHeight = static_cast<int>(std::round(textureRect.height()));
  if (textureWidth <= 0 || textureHeight <= 0) {
    return nullptr;
  }
  SamplingArgs samplingArgs = {TileMode::Decal, TileMode::Decal,
                               SamplingOptions(FilterMode::Linear), SrcRectConstraint::Fast};
  auto allocator = context->drawingAllocator();
  float verticalHalo = std::ceil(std::max(fineRadius.y, coarseRadius.y)) + 1.0f;
  auto horizontalRect = textureRect.makeOutset(0.0f, verticalHalo);
  horizontalRect.roundOut();
  auto horizontalHeight = static_cast<int>(std::round(horizontalRect.height()));
  auto horizontalTarget =
      RenderTargetProxy::Make(context, textureWidth, horizontalHeight, false, 1, false,
                              ImageOrigin::TopLeft, BackingFit::Exact);
  if (horizontalTarget == nullptr) {
    return nullptr;
  }
  float horizontalHalo = std::ceil(std::max(fineRadius.x, coarseRadius.x)) + 1.0f;
  auto sourceDrawRect = Rect::MakeLTRB(-horizontalHalo, -1.0f,
                                       static_cast<float>(textureWidth) + horizontalHalo,
                                       static_cast<float>(horizontalHeight) + 1.0f);
  Matrix horizontalMatrix = {};
  FPArgs sourceArgs = {};
  std::shared_ptr<Image> coreSource = nullptr;
  // makeRasterized() returns the image itself only when it is already rasterized.
  if (source->makeRasterized().get() == source.get()) {
    // A rasterized source locks the whole image regardless of drawRect, so the lock must be
    // bounded by scaling to the UDF core first.
    coreSource = source->makeScaled(coreWidth, coreHeight, SamplingOptions(FilterMode::Linear));
    if (coreSource == nullptr) {
      return nullptr;
    }
    coreSource = coreSource->makeRasterized();
    horizontalMatrix = Matrix::MakeTrans(textureRect.left, horizontalRect.top);
    sourceArgs = FPArgs(context, 0, sourceDrawRect);
  } else {
    // Other sources rasterize only the drawRect window at drawScale density, so they can sample
    // the visible UDF window directly from the full-resolution content.
    float sourceScaleX = static_cast<float>(source->width()) / static_cast<float>(coreWidth);
    float sourceScaleY = static_cast<float>(source->height()) / static_cast<float>(coreHeight);
    horizontalMatrix =
        Matrix::MakeAll(sourceScaleX, 0.0f, textureRect.left * sourceScaleX, 0.0f, sourceScaleY,
                        horizontalRect.top * sourceScaleY);
    float sourceDrawScale =
        std::max(static_cast<float>(coreWidth) / static_cast<float>(source->width()),
                 static_cast<float>(coreHeight) / static_cast<float>(source->height()));
    sourceArgs = FPArgs(context, 0, sourceDrawRect, sourceDrawScale);
  }
  const auto& fpSource = coreSource != nullptr ? coreSource : source;
  // Each tent loop needs its own child because emitting one child twice redeclares its uniforms.
  auto fineSource = FragmentProcessor::Make(fpSource, sourceArgs, samplingArgs, &horizontalMatrix);
  auto coarseSource = FragmentProcessor::Make(fpSource, sourceArgs, samplingArgs, &horizontalMatrix);
  auto horizontalProcessor = GlassUDFTentBlurFragmentProcessor::Make(
      allocator, std::move(fineSource), std::move(coarseSource), fineRadius.x, coarseRadius.x,
      GlassUDFBlurDirection::Horizontal, MaxTentRadius, false);
  if (horizontalProcessor == nullptr ||
      !context->drawingManager()->fillRTWithFP(horizontalTarget, std::move(horizontalProcessor),
                                               0)) {
    return nullptr;
  }
  auto verticalMatrix = Matrix::MakeTrans(0.0f, textureRect.top - horizontalRect.top);
  auto horizontalProxy = horizontalTarget->asTextureProxy();
  auto verticalFineSource =
      TiledTextureEffect::Make(allocator, horizontalProxy, samplingArgs, &verticalMatrix);
  auto verticalCoarseSource =
      TiledTextureEffect::Make(allocator, horizontalProxy, samplingArgs, &verticalMatrix);
  auto verticalTarget = RenderTargetProxy::Make(context, textureWidth, textureHeight, false, 1,
                                                false, ImageOrigin::TopLeft, BackingFit::Exact);
  if (verticalTarget == nullptr) {
    return nullptr;
  }
  auto verticalProcessor = GlassUDFTentBlurFragmentProcessor::Make(
      allocator, std::move(verticalFineSource), std::move(verticalCoarseSource), fineRadius.y,
      coarseRadius.y, GlassUDFBlurDirection::Vertical, MaxTentRadius, true);
  if (verticalProcessor == nullptr ||
      !context->drawingManager()->fillRTWithFP(verticalTarget, std::move(verticalProcessor), 0)) {
    return nullptr;
  }
  return TextureImage::Wrap(verticalTarget->asTextureProxy(), nullptr);
}

struct GlassShapeInfo {
  GlassShapeType type = GlassShapeType::AlphaMask;
  float cornerRadius = 0.0f;
  RRect shapeRRect = {};
  Path shapePath = {};
  bool hasPath = false;
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
  info.shapePath = path;
  info.hasPath = true;
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
  // Do not cache the frost filter: onDraw applies frost at a different scale
  // (contentScale * bgScale) to the downsampled background image, invalidating any cache here.
  float sigma = std::max(0.5f, (_frost / 100.0f) * MaxFrostSigma) * contentScale;
  auto filter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
  auto bounds = filter == nullptr ? srcRect : filter->filterBounds(srcRect);
  if (_refraction > 0 || _lightIntensity > 0) {
    float maxWidth =
        FloatNearlyZero(contentScale) ? srcRect.width() : srcRect.width() / contentScale;
    float maxHeight =
        FloatNearlyZero(contentScale) ? srcRect.height() : srcRect.height() / contentScale;
    for (auto owner : owners) {
      if (owner == nullptr) {
        continue;
      }
      auto ownerBounds = owner->getBounds(owner, false);
      maxWidth = std::max(maxWidth, ownerBounds.width());
      maxHeight = std::max(maxHeight, ownerBounds.height());
    }
    auto minHalf = std::min(maxWidth, maxHeight) * 0.5f;
    float refractionOutset =
        GetRefractionOutset(maxWidth, maxHeight, getRefractionFactor(), getDepthRatio(),
                             getDispersionFactor(), getGlassThickness(minHalf));
    refractionOutset = refractionOutset * contentScale + 1.0f;
    bounds.outset(refractionOutset, refractionOutset);
  }
  return bounds.makeOutset(1.0f, 1.0f);
}

Rect GlassStyle::filterBackgroundSharp(const Rect& srcRect, float) {
  return srcRect.makeOutset(1.0f, 1.0f);
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
  float frostDownscale = 1.0f;
  auto evaluationLimit = std::min(static_cast<float>(maxTextureSize), MaxBackgroundSize);
  auto clipBounds = GetCanvasLocalClipBounds(canvas);
  // Frost and refraction have bounded sampling radii, so evaluate against the visible clip
  // whenever it is known; subsets exceeding the texture limit fall back to the full background.
  if (clipBounds.has_value() && !clipBounds->isEmpty()) {
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
    // Refraction samples toward the layer interior (UDF gradient points inward), so the
    // sampling range never extends past the layer content bounds.
    refractInputRect.intersect(Rect::MakeWH(contentWidth, contentHeight));
    refractInputRect.roundOut();
    }
    auto backgroundInputRect = refractInputRect;
    auto fullResolutionBlur = getFrostFilter(input.contentScale * scaleRatioX);
    if (fullResolutionBlur != nullptr) {
      backgroundInputRect = fullResolutionBlur->filterBounds(refractInputRect);
      backgroundInputRect.outset(1.0f, 1.0f);
    }
    backgroundInputRect.scale(frostDownscale, frostDownscale);
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
  auto blurFilter = getFrostFilter(input.contentScale * scaleRatioX);
  if (blurFilter != nullptr) {
    Point blurOffset = {};
    auto frostedImage = bgImage->makeWithFilter(blurFilter, &blurOffset, nullptr);
    if (frostedImage != nullptr) {
      processedBg = frostedImage;
      processedOffset += blurOffset;
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
    float edgeSpanX = 1.0f;
    float edgeSpanY = 1.0f;
    Point udfTextureOrigin = {};
    std::shared_ptr<Image> maskImage = nullptr;
    if (shapeInfo.type == GlassShapeType::AlphaMask) {
      // Tent blur approximates the UDF while the allocated texture covers only the visible window
      // and the neighborhoods required by blur and shader sampling.
      // Use original layer bounds (not zoom-affected) for blur radius calculation.
      // depth 1-100 maps linearly to blurRadius 5-30. The 30.0 cap limits max UDF blur to avoid
      // overly wide transitions on large depth values.
      static constexpr float MAX_BLUR_RADIUS = 30.0f;
      static constexpr float MIN_BLUR_RADIUS = 5.0f;
      float blurRadius =
          std::max(std::min((_depth / 100.0f) * MAX_BLUR_RADIUS, MAX_BLUR_RADIUS), MIN_BLUR_RADIUS);
      float minHalf = std::min(origBounds.width(), origBounds.height()) * 0.5f;
      float effectiveBlurRadius = std::min(blurRadius, minHalf);
      // UDF resolution matches content pixel dimensions (1 texel = 1 content pixel), so the
      // effective resolution scales with zoom. The textureRect subset mechanism ensures only
      // the visible region + halo is actually rasterized.
      static constexpr float MAX_UDF_SHADER_HALO = MaxTentRadius * 0.5f + 1.0f;
      static constexpr float MAX_UDF_BLUR_HALO = MaxTentRadius + 1.0f;
      static constexpr float MAX_UDF_SIZE = 2048.0f;
      float sourceWidth = contentWidth;
      float sourceHeight = contentHeight;
      float sourceMaxDim = std::max(sourceWidth, sourceHeight);
      float maxAllowedDim =
          std::min(static_cast<float>(maxTextureSize), MAX_UDF_SIZE) -
          (MAX_UDF_SHADER_HALO + MAX_UDF_BLUR_HALO) * 2.0f;
      if (maxAllowedDim < 1.0f) {
        return;
      }
      float udfScale = 1.0f;
      if (sourceMaxDim > maxAllowedDim) {
        udfScale = maxAllowedDim / sourceMaxDim;
      }
      static constexpr float MIN_SHORT_SIDE_TEXELS = 256.0f;
      float sourceMinDim = std::min(sourceWidth, sourceHeight);
      if (sourceMinDim * udfScale < MIN_SHORT_SIDE_TEXELS) {
        udfScale = MIN_SHORT_SIDE_TEXELS / sourceMinDim;
      }
      int udfWidth = std::max(1, static_cast<int>(std::round(sourceWidth * udfScale)));
      int udfHeight = std::max(1, static_cast<int>(std::round(sourceHeight * udfScale)));
      // The UDF dimensions are rounded independently, so each axis needs its own layer-pixel
      // conversion ratio. Both values use origBounds so the shader still operates in layer space.
      udfPixelToLayerPixelX = origBounds.width() / static_cast<float>(udfWidth);
      udfPixelToLayerPixelY = origBounds.height() / static_cast<float>(udfHeight);
      // Blur radius in UDF pixel space. The horizontal pass samples input.content directly into
      // the target UDF core, so resizing does not require an intermediate texture.
      // The radius scales with udfScale, keeping the layer-space radius constant.
      float layerToSourceX = sourceWidth / origBounds.width();
      float layerToSourceY = sourceHeight / origBounds.height();
      Point fineRadius = {
          std::min(effectiveBlurRadius * udfScale * layerToSourceX,
                   static_cast<float>(MaxTentRadius)),
          std::min(effectiveBlurRadius * udfScale * layerToSourceY,
                   static_cast<float>(MaxTentRadius))};
      // Edge light radius is defined in layer space so the width stays constant across zoom.
      static constexpr float EDGE_RADIUS_IN_LAYER_PIXELS = 1.0f;
      Point coarseRadius = {
          std::min(EDGE_RADIUS_IN_LAYER_PIXELS / udfPixelToLayerPixelX,
                   static_cast<float>(MaxTentRadius)),
          std::min(EDGE_RADIUS_IN_LAYER_PIXELS / udfPixelToLayerPixelY,
                   static_cast<float>(MaxTentRadius))};
      // The span must come from the clamped radius, otherwise the shader would divide by a
      // different distance than the one the blur actually produced.
      edgeSpanX = coarseRadius.x * udfPixelToLayerPixelX;
      edgeSpanY = coarseRadius.y * udfPixelToLayerPixelY;

      auto visibleContentRect = Rect::MakeWH(contentWidth, contentHeight);
      if (clipBounds.has_value() && !visibleContentRect.intersect(*clipBounds)) {
        return;
      }
      float contentToUDFX = static_cast<float>(udfWidth) / contentWidth;
      float contentToUDFY = static_cast<float>(udfHeight) / contentHeight;
      auto visibleUDFRect =
          Rect::MakeLTRB(std::floor(visibleContentRect.left * contentToUDFX),
                         std::floor(visibleContentRect.top * contentToUDFY),
                         std::ceil(visibleContentRect.right * contentToUDFX),
                         std::ceil(visibleContentRect.bottom * contentToUDFY));
      float gradientSampleRadius = getDepthRatio() * 3.0f + 1.0f;
      float shaderHaloX =
          std::ceil(std::max(gradientSampleRadius, coarseRadius.x * 0.5f)) + 1.0f;
      float shaderHaloY =
          std::ceil(std::max(gradientSampleRadius, coarseRadius.y * 0.5f)) + 1.0f;
      auto fullTextureRect = Rect::MakeLTRB(-shaderHaloX, -shaderHaloY,
                                            static_cast<float>(udfWidth) + shaderHaloX,
                                            static_cast<float>(udfHeight) + shaderHaloY);
      auto textureRect = visibleUDFRect.makeOutset(shaderHaloX, shaderHaloY);
      if (!textureRect.intersect(fullTextureRect)) {
        return;
      }
      textureRect.roundOut();
      static constexpr float LOCAL_UDF_AREA_THRESHOLD = 0.7f;
      if (textureRect.area() >= fullTextureRect.area() * LOCAL_UDF_AREA_THRESHOLD) {
        textureRect = fullTextureRect;
        textureRect.roundOut();
      }
      udfTextureOrigin = {textureRect.left, textureRect.top};
      maskImage = MakeGlassUDFImage(context, input.content, udfWidth, udfHeight, textureRect,
                                    fineRadius, coarseRadius);
      if (!maskImage) {
        LOGE("GlassStyle: Failed to create alpha UDF.");
        return;
      }
    }
    Point sourceOrigin = {processedOffset.x / scaleRatioX, processedOffset.y / scaleRatioY};
    Point sourcePixelToContentPixel = {1.0f / scaleRatioX, 1.0f / scaleRatioY};
    Point layerPixelToSourcePixel = {input.contentScale * scaleRatioX,
                                     input.contentScale * scaleRatioY};
    auto filter = getRefractionFilter(shapeInfo.type, shapeInfo.cornerRadius, halfW, halfH,
                                      udfPixelToLayerPixelX, udfPixelToLayerPixelY, edgeSpanX,
                                      edgeSpanY, udfTextureOrigin, sourceOrigin,
                                      sourcePixelToContentPixel, layerPixelToSourcePixel,
                                      contentWidth, contentHeight, maskImage);
    Point refractOffset = {};
    auto refractedImage = processedBg->makeWithFilter(filter, &refractOffset, nullptr);
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
    // AlphaMask path: use the vector path for GPU-native AA when available, avoiding the
    // resolution-dependent alpha mask that produces stair-stepping on low-zoom diagonal edges.
    if (shapeInfo.hasPath) {
      auto drawPath = shapeInfo.shapePath;
      auto pathMatrix = Matrix::MakeScale(input.contentScale, input.contentScale);
      pathMatrix.postTranslate(-input.contentOffset.x, -input.contentOffset.y);
      drawPath.transform(pathMatrix);
      canvas->drawPath(drawPath, paint);
    } else {
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
}

std::shared_ptr<ImageFilter> GlassStyle::getFrostFilter(float contentScale) {
  if (frostFilter && FloatNearlyEqual(currentFrostScale, contentScale)) {
    return frostFilter;
  }
  currentFrostScale = contentScale;
  float sigma = std::max(0.5f, (_frost / 100.0f) * MaxFrostSigma) * contentScale;
  frostFilter = ImageFilter::Blur(sigma, sigma, TileMode::Mirror);
  return frostFilter;
}

void GlassStyle::invalidateFrostFilter() {
  frostFilter = nullptr;
  invalidateTransform();
}

std::shared_ptr<ImageFilter> GlassStyle::getRefractionFilter(
    GlassShapeType shapeType, float cornerRadius, float halfWidth, float halfHeight,
    float udfPixelToLayerPixelX, float udfPixelToLayerPixelY, float edgeSpanX, float edgeSpanY,
    const Point& udfTextureOrigin, const Point& sourceOrigin,
    const Point& sourcePixelToContentPixel, const Point& layerPixelToSourcePixel,
    float contentWidth, float contentHeight, std::shared_ptr<Image> maskImage) {
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
  params.frost = _frost;
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
    udfParams.edgeSpanX = edgeSpanX;
    udfParams.edgeSpanY = edgeSpanY;
    udfParams.textureOriginX = udfTextureOrigin.x;
    udfParams.textureOriginY = udfTextureOrigin.y;
    float depthScale = std::clamp(depthRatio / 0.1f, 0.0f, 1.0f);
    depthScale = depthScale * depthScale * (3.0f - 2.0f * depthScale);
    params.maxDisplacement = 0.999f * minHalf * refractionFactor * depthRatio * depthScale;
  }
  return std::make_shared<GlassRefractionImageFilter>(params, sdfParams, udfParams,
                                                      std::move(maskImage));
}

}  // namespace tgfx
