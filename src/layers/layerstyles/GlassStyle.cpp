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
#include "layers/CanvasUtils.h"
#include "layers/imagefilters/GlassRefractionImageFilter.h"
#include "layers/layerstyles/GlassShader.h"
#include "layers/layerstyles/GlassUDFImage.h"
#include "layers/processors/GlassRefractionFragmentProcessor.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Surface.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/StyledShape.h"

namespace tgfx {

// Coordinate mappings shared by both refraction paths: the background source texture's
// origin in content pixels, its texel-to-content scale, and the layer-to-source scale.
struct GlassStyle::BackgroundMapping {
  Point sourceOrigin;
  Point sourcePixelToContentPixel;
  Point layerPixelToSourcePixel;
  float contentWidth;
  float contentHeight;
};

// Sampling parameters of the refraction and edge-light UDF textures.
struct GlassStyle::UDFSampling {
  Point pixelToLayerPixel;
  Point edgeSpan;
  Point textureOrigin;
  Point edgePixelToLayerPixel;
  Point edgeTextureOrigin;
};

static constexpr float MaxFrostSigma = 50.0f;
static constexpr int MaxTentRadius = 64;
static constexpr float MaxBackgroundSize = 2048.0f;
// Fallback texture size limit used when no GPU context is available at record time. Kept equal to
// the UDF size cap so recorded intermediate sizes stay allocatable on any playback device.
static constexpr int DefaultMaxTextureSize = 2048;

// depthRatio below 0.1 scales the refraction magnitude down via smoothstep to avoid
// abrupt visual transitions at very shallow depth values.
static float GetDepthScale(float depthRatio) {
  float depthScale = std::clamp(depthRatio / 0.1f, 0.0f, 1.0f);
  return depthScale * depthScale * (3.0f - 2.0f * depthScale);
}

// Max UDF refraction displacement in layer pixels. Shared by GetRefractionOutset
// (background sampling range) and getUDFRefractionFilter (shader maxDisplacement) to
// guarantee both use the identical bound.
static float GetUDFMaxDisplacement(float minHalf, float refractionFactor, float depthRatio) {
  // 0.999 keeps displacement strictly inside the glass half-size so refraction
  // sampling never reads past the layer boundary.
  return 0.999f * minHalf * refractionFactor * depthRatio * GetDepthScale(depthRatio);
}

// SDF shapes cap the refraction displacement at refractionFactor * glassThickness in the shader
// (glassThickness is capped at the depth parameter); the sampling outset follows the same bound,
// extended only by the dispersion channel spread.
static float GetSDFRefractionOutset(float refractionFactor, float glassThickness,
                                    float dispersion) {
  return std::max(glassThickness * refractionFactor * (1.0f + dispersion), 1.0f);
}

// AlphaMask shapes displace by minHalf * refractionFactor * depthRatio in the shader, clamped to
// maxDisplacement; the outset uses the identical bound.
static float GetUDFRefractionOutset(float minHalf, float refractionFactor, float depthRatio,
                                    float dispersion) {
  return std::max(GetUDFMaxDisplacement(minHalf, refractionFactor, depthRatio) *
                      (1.0f + dispersion),
                  1.0f);
}

// Used when the shape type is not known yet (filterBackground runs before shape detection):
// cover both paths conservatively.
static float GetRefractionOutset(float width, float height, float refractionFactor,
                                 float depthRatio, float dispersion, float glassThickness) {
  auto minHalf = std::min(width, height) * 0.5f;
  return std::max(GetSDFRefractionOutset(refractionFactor, glassThickness, dispersion),
                  GetUDFRefractionOutset(minHalf, refractionFactor, depthRatio, dispersion));
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
  return bounds.makeOutset(1.0f, 1.0f);
}

Rect GlassStyle::filterBackgroundSharp(const Rect& srcRect, float contentScale) {
  if (_refraction <= 0 && _lightIntensity <= 0) {
    return srcRect.makeOutset(1.0f, 1.0f);
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
  float sigma = std::max(0.5f, (_frost / 100.0f) * MaxFrostSigma) * contentScale;
  refractionOutset += sigma * 2.0f + 1.0f;
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
  // The UDF generation is deferred to GlassUDFImage::lockTextureProxy, so a missing GPU context
  // at record time (e.g. recording into a Picture) no longer skips the effect. The texture size
  // limit is only used to size the intermediate images; without a context the default is capped at
  // the UDF size limit so the recorded layout never exceeds what playback can allocate.
  auto surface = canvas->getSurface();
  auto context = surface ? surface->getContext() : nullptr;
  auto maxTextureSize =
      context ? context->gpu()->limits()->maxTextureDimension2D : DefaultMaxTextureSize;
  auto contentWidth = static_cast<float>(input.content->width());
  auto contentHeight = static_cast<float>(input.content->height());
  auto origWidth = contentWidth / input.contentScale;
  auto origHeight = contentHeight / input.contentScale;
  auto origBounds = Rect::MakeWH(origWidth, origHeight);
  if (origBounds.isEmpty()) {
    return;
  }
  auto shapeInfo = DetectGlassShape(input);
  // The edge light is only rendered when one layer pixel spans more than two screen pixels;
  // below that the narrow light is under-sampled and adds cost without visible detail.
  bool edgeLightEnabled = input.contentScale > 2.0f;

  float scaleRatioX = 1.0f;
  float scaleRatioY = 1.0f;
  bool usesLocalEvaluation = false;
  Rect visibleRect = {};
  Rect refractInputRect = {};
  float frostDownscale = 1.0f;
  auto evaluationLimit = std::min(static_cast<float>(maxTextureSize), MaxBackgroundSize);
  auto clipBounds = GetClipBounds(canvas);

  // Apply the frost blur to the shared background snapshot once and cache the result so all
  // tiles reuse the same blurred image; subsetting happens after the blur, so no blur halo
  // needs to be added to the window below.
  {
    auto blurFilter = getFrostFilter(input.contentScale * scaleRatioX);
    if (blurFilter != nullptr &&
        (cachedFrostSource != bgImage ||
         !FloatNearlyEqual(cachedFrostContentScale, input.contentScale))) {
      Point blurOffset = {};
      auto frostedImage = bgImage->makeWithFilter(blurFilter, &blurOffset, nullptr);
      if (frostedImage != nullptr) {
        cachedFrostSource = bgImage;
        cachedFrostContentScale = input.contentScale;
        cachedFrostBlurOffset = blurOffset;
        cachedFrostedImage = std::move(frostedImage);
      }
    }
    if (cachedFrostSource == bgImage && cachedFrostedImage != nullptr) {
      bgImage = cachedFrostedImage;
      bgOffset += cachedFrostBlurOffset;
    }
  }

  // Downscale the whole blurred background once and cache the result so all tiles share the
  // same downscaled texture; subsetting after the downscale is a lazy view and adds no
  // intermediate texture per tile.
  static constexpr float MAX_FROST_AREA = 1024.0f * 1024.0f;
  if (_refraction > 0 || _lightIntensity > 0) {
    float fullArea =
        static_cast<float>(bgImage->width()) * static_cast<float>(bgImage->height());
    if (fullArea > MAX_FROST_AREA) {
      frostDownscale = std::sqrt(MAX_FROST_AREA / fullArea);
    }
  }
  if (frostDownscale < 1.0f) {
    if (cachedDownscaleSource != bgImage || !FloatNearlyEqual(cachedDownscale, frostDownscale)) {
      int scaledW = std::max(
          1, static_cast<int>(std::round(static_cast<float>(bgImage->width()) * frostDownscale)));
      int scaledH = std::max(
          1, static_cast<int>(std::round(static_cast<float>(bgImage->height()) * frostDownscale)));
      auto scaledBg = bgImage->makeScaled(scaledW, scaledH, SamplingOptions(FilterMode::Linear));
      if (scaledBg != nullptr) {
        cachedDownscaleSource = bgImage;
        cachedDownscale = frostDownscale;
        cachedDownscaledImage = std::move(scaledBg);
      }
    }
    if (cachedDownscaleSource == bgImage && cachedDownscaledImage != nullptr) {
      bgImage = cachedDownscaledImage;
      bgOffset.x *= cachedDownscale;
      bgOffset.y *= cachedDownscale;
      scaleRatioX *= cachedDownscale;
      scaleRatioY *= cachedDownscale;
    } else {
      frostDownscale = 1.0f;
    }
  }

  // Frost and refraction have bounded sampling radii, so evaluate against the visible clip
  // whenever it is known; subsets exceeding the texture limit fall back to the full background.
  if (clipBounds.has_value() && !clipBounds->isEmpty()) {
    visibleRect = *clipBounds;
    if (!visibleRect.intersect(Rect::MakeWH(contentWidth, contentHeight))) {
      return;
    }
    visibleRect.roundOut();

    // Subset the visible window from the downscaled blurred background. The window is computed
    // in the downscaled coordinate space; makeSubset is a lazy view and adds no texture.
    refractInputRect = visibleRect;
    if (_refraction > 0 || _lightIntensity > 0) {
      auto minHalf = std::min(origWidth, origHeight) * 0.5f;
      float refractionOutset =
          shapeInfo.type == GlassShapeType::AlphaMask
              ? GetUDFRefractionOutset(minHalf, getRefractionFactor(), getDepthRatio(),
                                       getDispersionFactor())
              : GetSDFRefractionOutset(getRefractionFactor(), getGlassThickness(minHalf),
                                       getDispersionFactor());
      refractionOutset = std::ceil(refractionOutset * input.contentScale + 1.0f);
      refractInputRect.outset(refractionOutset, refractionOutset);
      // Refraction samples toward the layer interior (UDF gradient points inward), so the
      // sampling range never extends past the layer content bounds.
      refractInputRect.intersect(Rect::MakeWH(contentWidth, contentHeight));
      refractInputRect.roundOut();
    }
    auto backgroundInputRect = refractInputRect;
    // The subset window never extends past the content bounds: the blurred background snapshot
    // may be larger, but the extra pixels are never displayed.
    backgroundInputRect.intersect(Rect::MakeWH(contentWidth, contentHeight));
    backgroundInputRect.roundOut();
    backgroundInputRect.scale(frostDownscale, frostDownscale);
    backgroundInputRect.roundOut();
    auto availableBackground =
        Rect::MakeXYWH(bgOffset.x, bgOffset.y, static_cast<float>(bgImage->width()),
                       static_cast<float>(bgImage->height()));
    bool fullCoverage = availableBackground.contains(backgroundInputRect);
    if (fullCoverage && static_cast<float>(bgImage->width()) <= evaluationLimit &&
        static_cast<float>(bgImage->height()) <= evaluationLimit) {
      // The whole background fits the texture limit and covers the sampling window: keep the
      // full image so the texture upload below is shared across tiles.
      usesLocalEvaluation = true;
    } else if (backgroundInputRect.intersect(availableBackground)) {
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
    float bgScaleX = static_cast<float>(scaledW) / static_cast<float>(bgImage->width());
    float bgScaleY = static_cast<float>(scaledH) / static_cast<float>(bgImage->height());
    bgImage = bgImage->makeScaled(scaledW, scaledH, SamplingOptions(FilterMode::Linear));
    if (bgImage == nullptr) {
      return;
    }
    bgOffset.x *= bgScaleX;
    bgOffset.y *= bgScaleY;
    scaleRatioX *= bgScaleX;
    scaleRatioY *= bgScaleY;
  }

  std::shared_ptr<Image> processedBg = bgImage;
  Point processedOffset = bgOffset;
  // Upload the processed background to a texture once and reuse it across tiles; the filter and
  // shader constructors below take the shared texture image, so per-tile sources are O(1).
  if (context != nullptr) {
    if (cachedBgTextureSource != processedBg) {
      cachedBgTextureSource = processedBg;
      cachedBgTextureImage = processedBg->makeTextureImage(context);
    }
    if (cachedBgTextureImage != nullptr) {
      processedBg = cachedBgTextureImage;
    }
  }
  // Step 2 & 3: Apply refraction with dispersion and lighting (computed entirely in GPU shader)
  std::shared_ptr<GlassRefractionImageFilter> glassFilter = nullptr;
  if (_refraction > 0 || _lightIntensity > 0) {
    // Use origBounds (not zoom-affected) for all refraction parameters so the effect is
    // zoom-invariant regardless of bgScale downsampling.
    float halfW = origBounds.width() * 0.5f;
    float halfH = origBounds.height() * 0.5f;
    BackgroundMapping mapping = {};
    mapping.sourceOrigin = {processedOffset.x / scaleRatioX, processedOffset.y / scaleRatioY};
    mapping.sourcePixelToContentPixel = {1.0f / scaleRatioX, 1.0f / scaleRatioY};
    mapping.layerPixelToSourcePixel = {input.contentScale * scaleRatioX,
                                       input.contentScale * scaleRatioY};
    mapping.contentWidth = contentWidth;
    mapping.contentHeight = contentHeight;
    if (shapeInfo.type == GlassShapeType::AlphaMask) {
      // Tent blur approximates the UDF. Use original layer bounds (not zoom-affected) for blur
      // radius calculation.
      // depth 1-100 maps linearly to blurRadius 5-30. The 30.0 cap limits max UDF blur to avoid
      // overly wide transitions on large depth values.
      static constexpr float MAX_BLUR_RADIUS = 30.0f;
      static constexpr float MIN_BLUR_RADIUS = 5.0f;
      float blurRadius =
          std::max(std::min((_depth / 100.0f) * MAX_BLUR_RADIUS, MAX_BLUR_RADIUS), MIN_BLUR_RADIUS);
      float minHalf = std::min(origBounds.width(), origBounds.height()) * 0.5f;
      auto visibleContentRect = Rect::MakeWH(contentWidth, contentHeight);
      if (clipBounds.has_value() && !visibleContentRect.intersect(*clipBounds)) {
        return;
      }
      float fullMaxDim = std::max(contentWidth, contentHeight);
      float maxTextureSizeF = static_cast<float>(maxTextureSize);
      if (maxTextureSizeF < 1.0f) {
        return;
      }
      // The refraction field needs a large layer-space reach but is low frequency, so its density
      // is anchored to the layer size and capped so the tent radius always fits MaxTentRadius.
      static constexpr float MAX_UDF_VISIBLE_DIM = 512.0f;
      float maxAllowedDim = std::min(maxTextureSizeF, MAX_UDF_VISIBLE_DIM);
      float udfScale = 1.0f;
      if (fullMaxDim > maxAllowedDim) {
        udfScale = maxAllowedDim / fullMaxDim;
      }
      static constexpr float MIN_SHORT_SIDE_TEXELS = 256.0f;
      float fullMinDim = std::min(contentWidth, contentHeight);
      if (fullMinDim * udfScale < MIN_SHORT_SIDE_TEXELS) {
        udfScale = MIN_SHORT_SIDE_TEXELS / fullMinDim;
      }
      int udfWidth = std::max(1, static_cast<int>(std::round(contentWidth * udfScale)));
      int udfHeight = std::max(1, static_cast<int>(std::round(contentHeight * udfScale)));
      // The UDF dimensions are rounded independently, so each axis needs its own layer-pixel
      // conversion ratio. Both values use origBounds so the shader still operates in layer space.
      float udfPixelToLayerPixelX = origBounds.width() / static_cast<float>(udfWidth);
      float udfPixelToLayerPixelY = origBounds.height() / static_cast<float>(udfHeight);
      // Blur radius in UDF pixel space. The horizontal pass samples input.content directly into
      // the target UDF core, so resizing does not require an intermediate texture.
      // The radius scales with udfScale, keeping the layer-space radius constant.
      float layerToSourceX = contentWidth / origBounds.width();
      float layerToSourceY = contentHeight / origBounds.height();
      // Low depth on a large layer leaves the refraction kernel below one UDF texel, which turns
      // the coverage ramp into per-texel steps. The internal floor lifts the layer-space radius so
      // the kernel always spans at least MIN_FINE_TENT_TEXELS texels.
      static constexpr float MIN_FINE_TENT_TEXELS = 2.0f;
      float minBlurRadius = MIN_FINE_TENT_TEXELS / (udfScale * layerToSourceX);
      blurRadius = std::max(blurRadius, minBlurRadius);
      float effectiveBlurRadius = std::min(blurRadius, minHalf);
      Point fineRadius = {std::min(effectiveBlurRadius * udfScale * layerToSourceX,
                                   static_cast<float>(MaxTentRadius)),
                          std::min(effectiveBlurRadius * udfScale * layerToSourceY,
                                   static_cast<float>(MaxTentRadius))};
      float gradientSampleRadius = getDepthRatio() * 3.0f + 1.0f;
      float fineHaloX = std::ceil(gradientSampleRadius) + 1.0f;
      float fineHaloY = std::ceil(gradientSampleRadius) + 1.0f;
      // The refraction texture covers the full layer once per frame and is shared across tiles.
      auto fineTextureRect = Rect::MakeLTRB(-fineHaloX, -fineHaloY,
                                            static_cast<float>(udfWidth) + fineHaloX,
                                            static_cast<float>(udfHeight) + fineHaloY);
      fineTextureRect.roundOut();
      Point udfTextureOrigin = {fineTextureRect.left, fineTextureRect.top};

      // The edge light field needs a narrow layer-space reach but high precision, so its texture
      // covers a grid cell instead of the whole layer. The grid is anchored to the content origin
      // and each cell is a fixed screen-space footprint converted to layer space; cells are
      // disjoint, so each tile samples the single cell it falls into. Cells persist across frames
      // while the content scale is unchanged, so panning reuses neighboring cells instead of
      // rebuilding.
      bool enableEdgeLighting = getLightIntensityFactor() > 0.0f && edgeLightEnabled;

      auto contentRect = Rect::MakeWH(contentWidth, contentHeight);
      auto tileClip = clipBounds.has_value() ? *clipBounds : contentRect;
      // The grid granularity is a fixed screen-space footprint; the clip bounds are in content
      // pixels, which map one-to-one with screen pixels here, so no scale conversion is needed.
      static constexpr float EDGE_CELL_SCREEN_PIXELS = 1024.0f;
      float cellSize = EDGE_CELL_SCREEN_PIXELS;
      // The cell is anchored to the content origin so its boundaries stay stable across frames, and
      // it is expanded outwards to fully contain the draw region. A single edge texture is bound
      // for the whole draw, and the draw region is neither one tile nor in phase with this grid:
      // continuous tiles are merged into one rect. Fragments outside the cell would sample past the
      // texture and read zero, turning the shape interior into a false edge.
      auto cell = Rect::MakeLTRB(std::floor(tileClip.left / cellSize) * cellSize,
                                 std::floor(tileClip.top / cellSize) * cellSize,
                                 std::ceil(tileClip.right / cellSize) * cellSize,
                                 std::ceil(tileClip.bottom / cellSize) * cellSize);
      if (!cell.intersect(contentRect)) {
        cell = tileClip;
        cell.intersect(contentRect);
      }
      int64_t cellKey =
          (static_cast<int64_t>(std::floor(cell.left / cellSize)) << 32) |
          (static_cast<int64_t>(std::floor(cell.top / cellSize)) & 0xFFFFFFFF);

      // The full-layer core density is fixed: each texel covers two content pixels, so the core
      // stays at half the content resolution regardless of the cell size or zoom.
      static constexpr float EDGE_CORE_SCALE = 0.5f;
      int edgeCoreWidth = std::max(1, static_cast<int>(std::round(contentWidth * EDGE_CORE_SCALE)));
      int edgeCoreHeight =
          std::max(1, static_cast<int>(std::round(contentHeight * EDGE_CORE_SCALE)));
      float edgePixelToLayerPixelX = 1.0f / (EDGE_CORE_SCALE * layerToSourceX);
      float edgePixelToLayerPixelY = 1.0f / (EDGE_CORE_SCALE * layerToSourceY);
      // The cell is expressed in the full-layer UDF space, so it must be scaled with the same
      // density as the core. Any other factor would shift the texture origin away from the
      // coordinates the shader derives from the full-layer mapping.
      float edgeScale = EDGE_CORE_SCALE;

      // The edge light width is fixed in layer space so it stays constant across zoom; a screen
      // pixel floor lifts the layer width when zooming out so the light never shrinks below five
      // screen pixels and degenerates into flickering dots.
      static constexpr float EDGE_LIGHT_LAYER_PIXELS = 2.0f;
      static constexpr float MIN_EDGE_LIGHT_SCREEN_PIXELS = 5.0f;
      auto edgeLightLayerPixels =
          std::max(EDGE_LIGHT_LAYER_PIXELS, MIN_EDGE_LIGHT_SCREEN_PIXELS / input.contentScale);
      // Tent kernels below four texels cannot represent a stable ramp, so the radius never drops
      // under that floor; the span below widens accordingly.
      static constexpr float MIN_COARSE_TENT_TEXELS = 4.0f;
      Point coarseRadius = {
          std::min(std::max(edgeLightLayerPixels / edgePixelToLayerPixelX, MIN_COARSE_TENT_TEXELS),
                   static_cast<float>(MaxTentRadius)),
          std::min(std::max(edgeLightLayerPixels / edgePixelToLayerPixelY, MIN_COARSE_TENT_TEXELS),
                   static_cast<float>(MaxTentRadius))};
      // The span must come from the clamped radius, otherwise the shader would divide by a
      // different distance than the one the blur actually produced.
      float edgeSpanX = coarseRadius.x * edgePixelToLayerPixelX;
      float edgeSpanY = coarseRadius.y * edgePixelToLayerPixelY;
      float edgeHaloX = std::ceil(coarseRadius.x * 0.5f) + 1.0f;
      float edgeHaloY = std::ceil(coarseRadius.y * 0.5f) + 1.0f;
      // The physical texture covers the cell (in the full-layer UDF space) plus the sampling
      // halo; the UDF coordinates keep the full-layer mapping so sampling stays uniform.
      auto edgeWindowUDF = cell;
      edgeWindowUDF.scale(edgeScale, edgeScale);
      auto edgeTextureRect = edgeWindowUDF.makeOutset(edgeHaloX, edgeHaloY);
      edgeTextureRect.roundOut();
      Point edgeTextureOrigin = {edgeTextureRect.left, edgeTextureRect.top};

      // The contour path reference is stable while the layer shape is unedited, so it serves as a
      // frame-invariant key; layers without a contour path fall back to the content image pointer.
      bool udfShapeMatch =
          shapeInfo.hasPath ? shapeInfo.shapePath.isSame(cachedUDFPath)
                            : (cachedUDFSource == input.content);
      bool cacheMatch = udfShapeMatch && FloatNearlyEqual(cachedUDFContentScale, input.contentScale) &&
                        FloatNearlyEqual(cachedUDFDepth, _depth) &&
                        FloatNearlyEqual(cachedUDFContentWidth, contentWidth) &&
                        FloatNearlyEqual(cachedUDFContentHeight, contentHeight);
      std::shared_ptr<Image> maskImage = nullptr;
      if (cacheMatch && cachedUDFImage != nullptr) {
        maskImage = cachedUDFImage;
      } else {
        maskImage = GlassUDFImage::Make(input.content, udfWidth, udfHeight, fineTextureRect,
                                        fineRadius, Point::Zero(),
                                        GlassUDFField::Refraction);
        if (!maskImage) {
          LOGE("GlassStyle: Failed to create refraction UDF.");
          return;
        }
        cachedUDFPath = shapeInfo.shapePath;
        cachedUDFSource = input.content;
        cachedUDFContentScale = input.contentScale;
        cachedUDFDepth = _depth;
        cachedUDFContentWidth = contentWidth;
        cachedUDFContentHeight = contentHeight;
        cachedUDFImage = maskImage;
      }
      std::shared_ptr<Image> edgeMaskImage = nullptr;
      if (enableEdgeLighting) {
        // Cell keys are grid positions in content pixels, and content pixels move with the content
        // scale, so the same key maps to a different region once the scale or the content changes.
        // Reusing an entry across such a change would sample the texture with a mismatched origin,
        // so the pool is invalidated on the same conditions as the full-layer UDF.
        bool gridMatch = cacheMatch && FloatNearlyEqual(cachedEdgeGridSize, cellSize);
        if (!gridMatch) {
          cachedEdgeUDFs.clear();
          cachedEdgeGridSize = cellSize;
        }
        auto it = cachedEdgeUDFs.find(cellKey);
        if (cacheMatch && gridMatch && it != cachedEdgeUDFs.end() &&
            it->second.windowRect.contains(cell)) {
          edgeMaskImage = it->second.udfImage;
        } else {
          edgeMaskImage = GlassUDFImage::Make(input.content, edgeCoreWidth, edgeCoreHeight,
                                              edgeTextureRect, Point::Zero(), coarseRadius,
                                              GlassUDFField::EdgeLight);
          if (!edgeMaskImage) {
            LOGE("GlassStyle: Failed to create edge light UDF.");
            return;
          }
          // Cap the pool so panning across many cells cannot grow it unboundedly; evicting the
          // whole pool is fine because a large cell count implies the view is moving.
          if (cachedEdgeUDFs.size() >= 16) {
            cachedEdgeUDFs.clear();
          }
          cachedEdgeUDFs[cellKey] = {cell, edgeMaskImage};
        }
      }
      UDFSampling udf = {};
      udf.pixelToLayerPixel = {udfPixelToLayerPixelX, udfPixelToLayerPixelY};
      udf.edgeSpan = {edgeSpanX, edgeSpanY};
      udf.textureOrigin = udfTextureOrigin;
      udf.edgePixelToLayerPixel = {edgePixelToLayerPixelX, edgePixelToLayerPixelY};
      udf.edgeTextureOrigin = edgeTextureOrigin;
      glassFilter = getUDFRefractionFilter(halfW, halfH, udf, mapping, std::move(maskImage),
                                           std::move(edgeMaskImage), edgeLightEnabled);
    } else {
      glassFilter = getSDFRefractionFilter(shapeInfo.type, shapeInfo.cornerRadius, halfW, halfH,
                                           mapping, edgeLightEnabled);
    }
  }

  float finalOffsetX = processedOffset.x / scaleRatioX;
  float finalOffsetY = processedOffset.y / scaleRatioY;
  float finalScaleX = 1.0f / scaleRatioX;
  float finalScaleY = 1.0f / scaleRatioY;

  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);

  auto imageMatrix =
      Matrix::MakeAll(finalScaleX, 0.0f, finalOffsetX, 0.0f, finalScaleY, finalOffsetY);
  // GlassShader renders the refraction shader directly at draw (screen) resolution, so the
  // SDF edge light is not baked into a downscaled offscreen texture by FilterImage.
  std::shared_ptr<Shader> glassShader = nullptr;
  if (glassFilter != nullptr) {
    glassShader = GlassShader::Make(std::move(glassFilter), processedBg, imageMatrix,
                                    SamplingOptions(FilterMode::Linear));
  } else {
    auto imageShader = Shader::MakeImageShader(processedBg, TileMode::Decal, TileMode::Decal,
                                               SamplingOptions(FilterMode::Linear));
    glassShader = imageShader->makeWithMatrix(imageMatrix);
  }
  paint.setShader(glassShader);

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
        maskImage =
            maskImage->makeScaled(maskWidth, maskHeight, SamplingOptions(FilterMode::Linear));
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

GlassRefractionParams GlassStyle::makeBaseRefractionParams(float halfW, float halfH,
                                                           const BackgroundMapping& mapping) const {
  GlassRefractionParams params = {};
  params.dispersion = getDispersionFactor();
  params.lightAngle = _lightAngle;
  params.lightIntensity = getLightIntensityFactor();
  params.origWidth = halfW * 2.0f;
  params.origHeight = halfH * 2.0f;
  params.glassUVScaleX = mapping.sourcePixelToContentPixel.x / mapping.contentWidth;
  params.glassUVScaleY = mapping.sourcePixelToContentPixel.y / mapping.contentHeight;
  params.glassUVOffsetX = mapping.sourceOrigin.x / mapping.contentWidth;
  params.glassUVOffsetY = mapping.sourceOrigin.y / mapping.contentHeight;
  params.layerPixelToSourcePixelX = mapping.layerPixelToSourcePixel.x;
  params.layerPixelToSourcePixelY = mapping.layerPixelToSourcePixel.y;
  params.frost = _frost;
  return params;
}

std::shared_ptr<GlassRefractionImageFilter> GlassStyle::getSDFRefractionFilter(
    GlassShapeType shapeType, float cornerRadius, float halfWidth, float halfHeight,
    const BackgroundMapping& mapping, bool edgeLightEnabled) {
  auto params = makeBaseRefractionParams(halfWidth, halfHeight, mapping);
  if (!edgeLightEnabled) {
    params.lightIntensity = 0.0f;
  }
  params.shapeType = shapeType;
  // Analytical SDF is an exact model, so displacement does not need clamping.
  params.maxDisplacement = 1.0e20f;

  float minHalf = std::min(halfWidth, halfHeight);
  GlassSDFGeometryParams sdfParams = {};
  sdfParams.halfW = halfWidth;
  sdfParams.halfH = halfHeight;
  sdfParams.cornerRadius = cornerRadius;
  sdfParams.glassThickness = getGlassThickness(minHalf);
  sdfParams.refractionFactor = getRefractionFactor();
  sdfParams.splay = std::clamp(_splay / 100.0f, 0.0f, 1.0f);
  sdfParams.depthRatio = getDepthRatio();
  return std::make_shared<GlassRefractionImageFilter>(params, sdfParams, GlassUDFGeometryParams{},
                                                      nullptr);
}

std::shared_ptr<GlassRefractionImageFilter> GlassStyle::getUDFRefractionFilter(
    float halfWidth, float halfHeight, const UDFSampling& udf, const BackgroundMapping& mapping,
    std::shared_ptr<Image> maskImage, std::shared_ptr<Image> edgeMaskImage,
    bool edgeLightEnabled) {
  auto params = makeBaseRefractionParams(halfWidth, halfHeight, mapping);
  if (!edgeLightEnabled) {
    params.lightIntensity = 0.0f;
  }
  params.shapeType = GlassShapeType::AlphaMask;
  float minHalf = std::min(halfWidth, halfHeight);
  params.maxDisplacement = GetUDFMaxDisplacement(minHalf, getRefractionFactor(), getDepthRatio());

  GlassUDFGeometryParams udfParams = {};
  udfParams.halfW = halfWidth;
  udfParams.halfH = halfHeight;
  udfParams.refractionFactor = getRefractionFactor();
  udfParams.splay = std::clamp(_splay / 100.0f, 0.0f, 1.0f);
  udfParams.depthRatio = getDepthRatio();
  udfParams.udfPixelToLayerPixelX = udf.pixelToLayerPixel.x;
  udfParams.udfPixelToLayerPixelY = udf.pixelToLayerPixel.y;
  udfParams.edgeSpanX = udf.edgeSpan.x;
  udfParams.edgeSpanY = udf.edgeSpan.y;
  udfParams.textureOriginX = udf.textureOrigin.x;
  udfParams.textureOriginY = udf.textureOrigin.y;
  udfParams.edgeTextureOriginX = udf.edgeTextureOrigin.x;
  udfParams.edgeTextureOriginY = udf.edgeTextureOrigin.y;
  udfParams.edgePixelToLayerPixelX = udf.edgePixelToLayerPixel.x;
  udfParams.edgePixelToLayerPixelY = udf.edgePixelToLayerPixel.y;
  return std::make_shared<GlassRefractionImageFilter>(params, GlassSDFGeometryParams{}, udfParams,
                                                      std::move(maskImage),
                                                      std::move(edgeMaskImage));
}

}  // namespace tgfx
