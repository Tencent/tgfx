/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/layerstyles/InnerShadowStyle.h"
#include "core/shaders/ShapeBlurShader.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

std::shared_ptr<InnerShadowStyle> InnerShadowStyle::Make(float offsetX, float offsetY,
                                                         float blurrinessX, float blurrinessY,
                                                         const Color& color) {
  return std::shared_ptr<InnerShadowStyle>(
      new InnerShadowStyle(offsetX, offsetY, blurrinessX, blurrinessY, color));
}

void InnerShadowStyle::setOffsetX(float offsetX) {
  if (_offsetX == offsetX) {
    return;
  }
  _offsetX = offsetX;
  invalidateFilter();
}

void InnerShadowStyle::setOffsetY(float offsetY) {
  if (_offsetY == offsetY) {
    return;
  }
  _offsetY = offsetY;
  invalidateFilter();
}

void InnerShadowStyle::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void InnerShadowStyle::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void InnerShadowStyle::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

void InnerShadowStyle::setSpread(float spread) {
  if (_spread == spread) {
    return;
  }
  _spread = spread;
  // Spread does not affect the cached ImageFilter. Only trigger a redraw to regenerate the
  // spread shape image.
  invalidateTransform();
}

InnerShadowStyle::InnerShadowStyle(float offsetX, float offsetY, float blurrinessX,
                                   float blurrinessY, const Color& color)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(color) {
}

Rect InnerShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  // When spread is non-zero, the shadow is drawn via a custom path without using the cached
  // filter. Inner shadow never expands the content bounds regardless of spread.
  if (!FloatNearlyZero(_spread)) {
    return srcRect;
  }

  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  return filter->filterBounds(srcRect);
}

uint32_t InnerShadowStyle::extraSourceType() const {
  auto type = !FloatNearlyZero(_spread) ? LayerStyleExtraSourceType::Contour
                                        : LayerStyleExtraSourceType::None;
  return static_cast<uint32_t>(type);
}

void InnerShadowStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                              BlendMode blendMode) {
  if (!FloatNearlyZero(_spread)) {
    drawWithSpread(canvas, input, alpha, blendMode);
    return;
  }

  auto filter = getShadowFilter(input.contentScale);
  if (!filter) {
    return;
  }
  auto content = input.content->makeWithFilter(filter);
  DEBUG_ASSERT(content != nullptr);
  if (content == nullptr) {
    return;
  }

  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  // Use nearest filtering when there's no blur to avoid edge artifacts caused by linear
  // interpolation. When the texture is scaled up, linear filtering produces intermediate alpha
  // values at edges, which causes visible gray borders in the inner shadow.
  auto sampling = SamplingOptions();
  if (FloatNearlyZero(_blurrinessX) && FloatNearlyZero(_blurrinessY)) {
    sampling = SamplingOptions(FilterMode::Nearest, MipmapMode::None);
  }
  canvas->drawImage(content, sampling, &paint);
}

void InnerShadowStyle::drawWithSpread(Canvas* canvas, const LayerStyleInput& input, float alpha,
                                      BlendMode blendMode) {
  if (drawAnalytic(canvas, input, alpha, blendMode)) {
    return;
  }
  auto shapeResult = SpreadUtils::MakeSpreadShapeImage(input, 0);
  auto spreadShapeResult = SpreadUtils::MakeSpreadShapeImage(input, -_spread);

  // Use nearest filtering when there's no blur to avoid edge artifacts caused by linear
  // interpolation. When the texture is scaled up, linear filtering produces intermediate alpha
  // values at edges, which causes visible gray borders in the inner shadow.
  auto sampling = SamplingOptions();
  if (FloatNearlyZero(_blurrinessX) && FloatNearlyZero(_blurrinessY)) {
    sampling = SamplingOptions(FilterMode::Nearest, MipmapMode::None);
  }

  if (spreadShapeResult.collapsed) {
    // The mask fully collapsed — shadow fills the entire content area.
    if (!shapeResult.image) {
      return;
    }
    Paint paint = {};
    paint.setBlendMode(blendMode);
    paint.setAlpha(alpha);
    paint.setColorFilter(ColorFilter::Blend(_color, BlendMode::SrcIn));
    canvas->drawImage(shapeResult.image, shapeResult.offset.x, shapeResult.offset.y, sampling,
                      &paint);
    return;
  }
  if (!shapeResult.image || !spreadShapeResult.image) {
    return;
  }

  auto scale = input.contentScale;
  auto blurFilter = ImageFilter::Blur(_blurrinessX * scale, _blurrinessY * scale);
  auto maskImage = spreadShapeResult.image;
  auto maskOffset = spreadShapeResult.offset;
  if (blurFilter) {
    Point blurOffset = {};
    auto filtered = spreadShapeResult.image->makeWithFilter(blurFilter, &blurOffset);
    if (filtered) {
      maskImage = filtered;
      maskOffset.offset(blurOffset.x, blurOffset.y);
    }
  }

  // The mask shader samples in content-local coordinates. maskOffset positions the mask image
  // relative to the content origin, and the shadow offset shifts it further.
  auto maskShader = Shader::MakeImageShader(maskImage, TileMode::Decal, TileMode::Decal, sampling);
  auto offsetX = maskOffset.x + _offsetX * scale;
  auto offsetY = maskOffset.y + _offsetY * scale;
  auto offsetMaskShader = maskShader->makeWithMatrix(Matrix::MakeTrans(offsetX, offsetY));

  // Draw shape with shadow color, masked by the inverted blurred spread shape.
  Paint paint = {};
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  paint.setColorFilter(ColorFilter::Blend(_color, BlendMode::SrcIn));
  paint.setMaskFilter(MaskFilter::MakeShader(offsetMaskShader, true));
  canvas->drawImage(shapeResult.image, shapeResult.offset.x, shapeResult.offset.y, sampling,
                    &paint);
}

bool InnerShadowStyle::drawAnalytic(Canvas* canvas, const LayerStyleInput& input, float alpha,
                                    BlendMode blendMode) {
  // Only reachable with a non-zero spread: the analytic form derives both shapes from contentShape,
  // which describes this layer's own outline and omits the contents of its children. That matches
  // the spread path, which rasterizes contentShape alone, but not the no-spread path, which filters
  // the full content image.
  DEBUG_ASSERT(!FloatNearlyZero(_spread));
  // Vector exporters (SVG, PDF) translate the recorded draws into their own primitives and can
  // represent a blur as a native filter, but not a custom shader: routing them through the analytic
  // path would degrade a scalable feGaussianBlur into a rasterized mask. They record without a
  // Surface, which is what distinguishes them from GPU rendering.
  if (canvas->getSurface() == nullptr) {
    return false;
  }
  // The closed form only describes an axis-aligned shape. The geometry check inside
  // MakeAnalyticShape covers the shape's own matrix, but the canvas may carry a rotation, skew or
  // perspective of its own (e.g. a layer placed with setMatrix3D), which would shear the analytic
  // shapes while the filter path still rasterizes the real outline.
  if (!canvas->getMatrix().rectStaysRect()) {
    return false;
  }
  const auto sigmaX = _blurrinessX * input.contentScale;
  const auto sigmaY = _blurrinessY * input.contentScale;
  // A zero sigma degenerates the kernel; the filter path already handles the hard-edged case,
  // including its nearest-neighbour sampling.
  if (FloatNearlyZero(sigmaX) || FloatNearlyZero(sigmaY)) {
    return false;
  }
  // The mask is the layer's own shape and the shadow is that shape inset by the spread, matching the
  // two shape images the filter path rasterizes.
  auto mask = SpreadUtils::MakeAnalyticShape(input, 0.0f);
  if (!mask.has_value()) {
    return false;
  }
  auto shadow = SpreadUtils::MakeAnalyticShape(input, -_spread);
  if (!shadow.has_value()) {
    return false;
  }
  const auto maskBounds = mask->rRect.rect();
  const auto shadowBounds = shadow->rRect.rect();
  const auto offsetX = _offsetX * input.contentScale;
  const auto offsetY = _offsetY * input.contentScale;
  // The offset moves the shadow only, so it is folded into the center difference rather than applied
  // to the canvas: translating the canvas would drag the mask along with it.
  const auto shadowCenterOffset =
      Point::Make(shadowBounds.centerX() - maskBounds.centerX() + offsetX,
                  shadowBounds.centerY() - maskBounds.centerY() + offsetY);
  // Geometry, sigma and the canvas all live in content pixels here, so no extra scale is needed.
  auto shader = ShapeBlurShader::MakeInner(mask->rRect, shadow->rRect, shadowCenterOffset, sigmaX,
                                           sigmaY, _color, 1.0f);
  if (shader == nullptr) {
    return false;
  }

  Paint paint = {};
  paint.setShader(std::move(shader));
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  // The shadow never reaches outside the shape, so the mask bounds are the whole affected area. This
  // matches filterBounds returning srcRect unchanged.
  canvas->drawRect(maskBounds, paint);
  return true;
}

std::shared_ptr<ImageFilter> InnerShadowStyle::getShadowFilter(float scale) {
  if (shadowFilter && scale == currentScale) {
    return shadowFilter;
  }
  shadowFilter = ImageFilter::InnerShadowOnly(_offsetX * scale, _offsetY * scale,
                                              _blurrinessX * scale, _blurrinessY * scale, _color);
  currentScale = scale;
  return shadowFilter;
}

void InnerShadowStyle::invalidateFilter() {
  shadowFilter = nullptr;
  currentScale = 0.0f;
  invalidateTransform();
}

}  // namespace tgfx
