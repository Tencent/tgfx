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
#include "core/shaders/RRectInnerShadowShader.h"
#include "core/shaders/RectInnerShadowShader.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "layers/layerstyles/AnalyticShadowUtils.h"
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
  // contentShape covers only the layer's own content, not its children: a shadow derived from it
  // follows the layer itself, while the no-spread shadow follows the whole subtree. Only the
  // spread case can therefore be evaluated in closed form.
  // TODO: let contentShape describe the whole subtree, then lift this restriction.
  if (tryDrawAnalytic(canvas, input, alpha, blendMode)) {
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

// The shader antialiases the shadow's outer edge, the layer's own outline, instead of following
// the layer's allowsEdgeAntialiasing flag: a hard edge there would let the shadow cover up to
// half a pixel more than the outline.
bool InnerShadowStyle::tryDrawAnalytic(Canvas* canvas, const LayerStyleInput& input, float alpha,
                                       BlendMode blendMode) {
  DEBUG_ASSERT(!FloatNearlyZero(_spread));
  const auto sigmaX = _blurrinessX * input.contentScale;
  const auto sigmaY = _blurrinessY * input.contentScale;
  // The closed form normalizes by sigma per axis, so a zero sigma cannot be expressed. Supporting
  // an axis with zero blur would require additional shader branches, whose benefit has not been
  // validated.
  if (FloatNearlyZero(sigmaX) || FloatNearlyZero(sigmaY)) {
    return false;
  }
  // The shadow shape is the layer's own outline and the mask is that shape inset by the spread.
  const auto shadowShape = AnalyticShadowUtils::MakeShadowShape(input, 0.0f);
  if (!shadowShape.has_value()) {
    return false;
  }
  auto maskShape = AnalyticShadowUtils::MakeShadowShape(input, -_spread);
  if (!maskShape.has_value()) {
    return false;
  }
  // A collapsed mask means the inner shadow covers the entire shadow area.
  if (maskShape->rect().isEmpty()) {
    Paint paint = {};
    paint.setColor(_color);
    paint.setBlendMode(blendMode);
    paint.setAlpha(alpha);
    paint.setAntiAlias(true);
    if (shadowShape->isRect()) {
      canvas->drawRect(shadowShape->rect(), paint);
    } else {
      canvas->drawRRect(*shadowShape, paint);
    }
    return true;
  }

  // radii()[0] stands for all four corners: AnalyticShadowUtils::MakeShadowShape never returns a
  // complex RRect.
  DEBUG_ASSERT(!shadowShape->isComplex() && !maskShape->isComplex());
  // The spread insets the shape symmetrically, so both shapes are concentric up to rounding. The
  // tolerance is a hundredth of a pixel, well above the rounding of coordinates in the range the
  // content image covers.
  DEBUG_ASSERT(
      FloatNearlyEqual(maskShape->rect().centerX(), shadowShape->rect().centerX(), 0.01f) &&
      FloatNearlyEqual(maskShape->rect().centerY(), shadowShape->rect().centerY(), 0.01f));
  // The offset moves the shadow only, so it is applied to the mask shape rather than to the
  // canvas: translating the canvas would drag the shadow shape along with it.
  maskShape->offset(_offsetX * input.contentScale, _offsetY * input.contentScale);

  std::shared_ptr<Shader> shader = nullptr;
  if (shadowShape->isRect() && maskShape->isRect()) {
    shader =
        RectInnerShadowShader::Make(shadowShape->rect(), maskShape->rect(), sigmaX, sigmaY, _color);
  } else {
    shader = RRectInnerShadowShader::Make(shadowShape->rect(), shadowShape->radii()[0],
                                          maskShape->rect(), maskShape->radii()[0], sigmaX, sigmaY,
                                          _color);
  }
  DEBUG_ASSERT(shader != nullptr);
  if (shader == nullptr) {
    return false;
  }

  // The closed form needs the rect axis-aligned, and the shader gets that by mapping the
  // coordinates it is given back into the shape's own space, so a rotated or skewed canvas matrix
  // needs no handling here.
  Paint paint = {};
  paint.setShader(std::move(shader));
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  canvas->drawRect(shadowShape->rect(), paint);
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
