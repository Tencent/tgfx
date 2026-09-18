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

#include "tgfx/layers/layerstyles/DropShadowStyle.h"
#include "core/shaders/RRectBlurShader.h"
#include "core/shaders/RectBlurShader.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "layers/layerstyles/AnalyticShadowUtils.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"

namespace tgfx {

std::shared_ptr<DropShadowStyle> DropShadowStyle::Make(float offsetX, float offsetY,
                                                       float blurrinessX, float blurrinessY,
                                                       const Color& color, bool showBehindLayer) {
  return std::shared_ptr<DropShadowStyle>(
      new DropShadowStyle(offsetX, offsetY, blurrinessX, blurrinessY, color, showBehindLayer));
}

void DropShadowStyle::setOffsetX(float offsetX) {
  if (_offsetX == offsetX) {
    return;
  }
  _offsetX = offsetX;
  invalidateFilter();
}

void DropShadowStyle::setOffsetY(float offsetY) {
  if (_offsetY == offsetY) {
    return;
  }
  _offsetY = offsetY;
  invalidateFilter();
}

void DropShadowStyle::setBlurrinessX(float blurrinessX) {
  if (_blurrinessX == blurrinessX) {
    return;
  }
  _blurrinessX = blurrinessX;
  invalidateFilter();
}

void DropShadowStyle::setBlurrinessY(float blurrinessY) {
  if (_blurrinessY == blurrinessY) {
    return;
  }
  _blurrinessY = blurrinessY;
  invalidateFilter();
}

void DropShadowStyle::setColor(const Color& color) {
  if (_color == color) {
    return;
  }
  _color = color;
  invalidateFilter();
}

void DropShadowStyle::setShowBehindLayer(bool showBehindLayer) {
  if (_showBehindLayer == showBehindLayer) {
    return;
  }
  _showBehindLayer = showBehindLayer;
  invalidateTransform();
}

void DropShadowStyle::setSpread(float spread) {
  if (_spread == spread) {
    return;
  }
  _spread = spread;
  // Spread does not affect the cached ImageFilter. Only trigger a redraw to regenerate the
  // spread shape image.
  invalidateTransform();
}

DropShadowStyle::DropShadowStyle(float offsetX, float offsetY, float blurrinessX, float blurrinessY,
                                 const Color& color, bool showBehindLayer)
    : _offsetX(offsetX), _offsetY(offsetY), _blurrinessX(blurrinessX), _blurrinessY(blurrinessY),
      _color(color), _showBehindLayer(showBehindLayer) {
}

Rect DropShadowStyle::filterBounds(const Rect& srcRect, float contentScale) {
  auto filter = getShadowFilter(contentScale);
  if (!filter) {
    return srcRect;
  }
  auto bounds = srcRect;
  if (!FloatNearlyZero(_spread)) {
    bounds.outset(_spread * contentScale, _spread * contentScale);
  }
  return filter->filterBounds(bounds);
}

uint32_t DropShadowStyle::extraSourceType() const {
  if (!_showBehindLayer || !FloatNearlyZero(_spread)) {
    return static_cast<uint32_t>(LayerStyleExtraSourceType::Contour);
  }
  return static_cast<uint32_t>(LayerStyleExtraSourceType::None);
}

bool DropShadowStyle::tryDrawAnalytic(Canvas* canvas, const LayerStyleInput& input, float alpha,
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
  const auto shape = AnalyticShadowUtils::MakeShadowShape(input, _spread);
  if (!shape.has_value()) {
    return false;
  }
  // A negative spread can erase the shadow shape entirely, leaving nothing to draw.
  if (shape->rect().isEmpty()) {
    return true;
  }
  // radii()[0] stands for all four corners: AnalyticShadowUtils::MakeShadowShape never returns a
  // complex RRect.
  DEBUG_ASSERT(!shape->isComplex());
  std::shared_ptr<Shader> shader = nullptr;
  if (shape->isRect()) {
    shader = RectBlurShader::Make(shape->rect(), sigmaX, sigmaY, _color);
  } else {
    shader = RRectBlurShader::Make(shape->rect(), shape->radii()[0], sigmaX, sigmaY, _color);
  }
  DEBUG_ASSERT(shader != nullptr);
  if (shader == nullptr) {
    return false;
  }

  // The closed form needs the rect axis-aligned, and the shader gets that by mapping the
  // coordinates it is given back into the shape's own space, so a rotated or skewed canvas matrix
  // needs no handling here.
  auto drawRect = shape->rect().makeOutset(2.0f * sigmaX, 2.0f * sigmaY);
  const auto offsetX = _offsetX * input.contentScale;
  const auto offsetY = _offsetY * input.contentScale;
  Paint paint = {};
  auto* knockoutContour =
      _showBehindLayer ? nullptr : input.findExtraSource(StyleInputSource::Type::Contour);
  if (knockoutContour != nullptr && knockoutContour->image() != nullptr) {
    auto contourShader =
        Shader::MakeImageShader(knockoutContour->image(), TileMode::Decal, TileMode::Decal, {});
    auto contourOffset = knockoutContour->imageOffset();
    // The canvas translation below moves the mask along with the shadow, so the offset is
    // cancelled here.
    auto matrixShader = contourShader->makeWithMatrix(
        Matrix::MakeTrans(contourOffset.x - offsetX, contourOffset.y - offsetY));
    paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
  }
  paint.setShader(std::move(shader));
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  AutoCanvasRestore restoreCanvas(canvas);
  canvas->translate(offsetX, offsetY);
  canvas->drawRect(drawRect, paint);
  return true;
}

void DropShadowStyle::onDraw(Canvas* canvas, const LayerStyleInput& input, float alpha,
                             BlendMode blendMode) {
  Point offset = {};
  auto filter = getShadowFilter(input.contentScale);
  if (!filter) {
    return;
  }
  std::shared_ptr<Image> filterSource = input.content;
  Point filterSourceOffset = {};
  if (!FloatNearlyZero(_spread)) {
    // contentShape covers only the layer's own content, not its children: a shadow derived from it
    // follows the layer itself, while the no-spread shadow follows the whole subtree. Only the
    // spread case can therefore be evaluated in closed form.
    // TODO: let contentShape describe the whole subtree, then lift this restriction.
    if (tryDrawAnalytic(canvas, input, alpha, blendMode)) {
      return;
    }
    auto spreadImage = SpreadUtils::MakeSpreadShapeImage(input, _spread);
    // The spread shadow is drawn from the spread shape image. When the vector shape is unavailable
    // (e.g. a group layer with only children) or exceeds the content image, the spread cannot be
    // applied; skip drawing rather than falling back, since filterBounds cannot reflect the
    // fallback geometry.
    if (spreadImage.collapsed || spreadImage.image == nullptr) {
      return;
    }
    filterSource = std::move(spreadImage.image);
    filterSourceOffset = spreadImage.offset;
  }
  DEBUG_ASSERT(filterSource != nullptr);
  auto shadowImage = filterSource->makeWithFilter(filter, &offset);
  DEBUG_ASSERT(shadowImage != nullptr);
  if (shadowImage == nullptr) {
    return;
  }

  // Use nearest filtering when there's no blur to avoid edge artifacts caused by linear
  // interpolation. When the texture is scaled up, linear filtering produces intermediate alpha
  // values at edges, which causes visible borders in the shadow.
  auto sampling = (FloatNearlyZero(_blurrinessX) && FloatNearlyZero(_blurrinessY))
                      ? SamplingOptions(FilterMode::Nearest, MipmapMode::None)
                      : SamplingOptions();
  Paint paint = {};
  auto* knockoutContour =
      _showBehindLayer ? nullptr : input.findExtraSource(StyleInputSource::Type::Contour);
  if (knockoutContour != nullptr && knockoutContour->image() != nullptr) {
    auto shader = Shader::MakeImageShader(knockoutContour->image(), TileMode::Decal,
                                          TileMode::Decal, sampling);
    auto contourOffset = knockoutContour->imageOffset();
    auto matrixShader = shader->makeWithMatrix(Matrix::MakeTrans(contourOffset.x, contourOffset.y));
    paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
  }
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  canvas->drawImage(shadowImage, filterSourceOffset.x + offset.x, filterSourceOffset.y + offset.y,
                    sampling, &paint);
}

std::shared_ptr<ImageFilter> DropShadowStyle::getShadowFilter(float scale) {
  if (shadowFilter && scale == currentScale) {
    return shadowFilter;
  }

  shadowFilter = ImageFilter::DropShadowOnly(_offsetX * scale, _offsetY * scale,
                                             _blurrinessX * scale, _blurrinessY * scale, _color);
  currentScale = scale;

  return shadowFilter;
}

void DropShadowStyle::invalidateFilter() {
  shadowFilter = nullptr;
  invalidateTransform();
}

}  // namespace tgfx
