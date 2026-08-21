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

bool DropShadowStyle::drawAnalytic(Canvas* canvas, const LayerStyleInput& input, float alpha,
                                   BlendMode blendMode) {
  DEBUG_ASSERT(!FloatNearlyZero(_spread));
  // The closed form convolves the shape along its own x and y axes, while the blurriness is defined
  // along the canvas axes. The two only agree while the axes stay parallel, so a canvas carrying
  // rotation, skew or perspective (e.g. a layer placed with setMatrix3D) would apply the blur along
  // the wrong directions. Quarter-turn rotations are still accepted: the supported shapes are
  // symmetric about both center axes, so the axes swap without tilting.
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
  auto analytic = MakeAnalyticShadowShape(input, _spread);
  if (!analytic.has_value()) {
    return false;
  }
  auto [shapeRect, shapeRadius] = *analytic;
  // Geometry, sigma and the canvas all live in content pixels here, so no extra scale is needed. A
  // sharp shape has its own closed form, without the corner term.
  std::shared_ptr<Shader> shader = nullptr;
  if (FloatNearlyZero(shapeRadius.x) && FloatNearlyZero(shapeRadius.y)) {
    shader = RectBlurShader::Make(shapeRect, sigmaX, sigmaY, _color, 1.0f);
  } else {
    shader = RRectBlurShader::Make(shapeRect, shapeRadius, sigmaX, sigmaY, _color, 1.0f);
  }
  if (shader == nullptr) {
    return false;
  }

  auto drawRect = shapeRect.makeOutset(2.0f * sigmaX, 2.0f * sigmaY);
  const auto offsetX = _offsetX * input.contentScale;
  const auto offsetY = _offsetY * input.contentScale;
  Paint paint = {};
  auto* contour = input.findExtraSource(StyleInputSource::Type::Contour);
  if (!_showBehindLayer && contour != nullptr && contour->image() != nullptr) {
    auto contourShader =
        Shader::MakeImageShader(contour->image(), TileMode::Decal, TileMode::Decal, {});
    auto contourOffset = contour->imageOffset();
    // The canvas is translated by the shadow offset before drawing, which would drag the contour
    // mask along with the shadow. The mask has to stay on the layer itself, so the offset is
    // cancelled here: only the shadow moves, the knockout does not.
    auto matrixShader = contourShader->makeWithMatrix(
        Matrix::MakeTrans(contourOffset.x - offsetX, contourOffset.y - offsetY));
    paint.setMaskFilter(MaskFilter::MakeShader(matrixShader, true));
  }
  paint.setShader(std::move(shader));
  paint.setBlendMode(blendMode);
  paint.setAlpha(alpha);
  // The offset moves the shadow only; translating the canvas keeps the shader and the draw rect in
  // the same space, so the geometry stays centred on the shape.
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
    // The analytic path draws from contentShape, the layer's own outline without its children. Only
    // the spread path matches that, since it rasterizes contentShape alone, while the no-spread path
    // blurs the full content image.
    if (drawAnalytic(canvas, input, alpha, blendMode)) {
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
  auto* contour = input.findExtraSource(StyleInputSource::Type::Contour);
  if (!_showBehindLayer && contour != nullptr && contour->image() != nullptr) {
    auto shader =
        Shader::MakeImageShader(contour->image(), TileMode::Decal, TileMode::Decal, sampling);
    auto contourOffset = contour->imageOffset();
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
