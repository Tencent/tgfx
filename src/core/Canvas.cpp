/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Canvas.h"
#include "core/DrawContext.h"
#include "core/LayerUnrollContext.h"
#include "core/Records.h"
#include "core/utils/Log.h"
#include "core/utils/SimpleTextShaper.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
Canvas::Canvas(DrawContext* drawContext) : drawContext(drawContext) {
  mcState = std::make_unique<MCState>();
}

Canvas::Canvas(DrawContext* drawContext, const Path& initClip) : drawContext(drawContext) {
  mcState = std::make_unique<MCState>(initClip);
}

Surface* Canvas::getSurface() const {
  return drawContext->getSurface();
}

void Canvas::save() {
  mcStack.push(std::make_unique<MCState>(*mcState));
}

void Canvas::restore() {
  if (mcStack.empty()) {
    return;
  }
  *mcState = *mcStack.top();
  mcStack.pop();
}

size_t Canvas::getSaveCount() const {
  return mcStack.size();
}

void Canvas::restoreToCount(size_t saveCount) {
  auto n = mcStack.size() - saveCount;
  for (size_t i = 0; i < n; i++) {
    restore();
  }
}

void Canvas::translate(float dx, float dy) {
  mcState->matrix.preTranslate(dx, dy);
}

void Canvas::scale(float sx, float sy) {
  mcState->matrix.preScale(sx, sy);
}

void Canvas::rotate(float degrees) {
  mcState->matrix.preRotate(degrees);
}

void Canvas::rotate(float degrees, float px, float py) {
  Matrix m = {};
  m.setRotate(degrees, px, py);
  mcState->matrix.preConcat(m);
}

void Canvas::skew(float sx, float sy) {
  mcState->matrix.preSkew(sx, sy);
}

void Canvas::concat(const Matrix& matrix) {
  mcState->matrix.preConcat(matrix);
}

void Canvas::setMatrix(const Matrix& matrix) {
  mcState->matrix = matrix;
}

void Canvas::resetMatrix() {
  mcState->matrix.reset();
}

const Matrix& Canvas::getMatrix() const {
  return mcState->matrix;
}

const Path& Canvas::getTotalClip() const {
  return mcState->clip;
}

void Canvas::clipRect(const tgfx::Rect& rect) {
  Path path = {};
  path.addRect(rect);
  clipPath(path);
}

void Canvas::clipPath(const Path& path) {
  auto clipPath = path;
  clipPath.transform(mcState->matrix);
  mcState->clip.addPath(clipPath, PathOp::Intersect);
}

void Canvas::resetMCState() {
  mcState = std::make_unique<MCState>();
  std::stack<std::unique_ptr<MCState>>().swap(mcStack);
}

void Canvas::clear() {
  auto& clip = mcState->clip;
  if (clip.isEmpty()) {
    if (clip.isInverseFillType()) {
      drawContext->clear();
    }
    return;
  }
  auto rect = clip.getBounds();
  FillStyle style = {};
  style.color = Color::Transparent();
  style.blendMode = BlendMode::Src;
  MCState state = {Matrix::I(), clip};
  drawContext->drawRect(rect, state, style);
}

void Canvas::clearRect(const Rect& rect, const Color& color) {
  Paint paint;
  paint.setColor(color);
  paint.setBlendMode(BlendMode::Src);
  drawRect(rect, paint);
}

static FillStyle CreateFillStyle(const Paint& paint) {
  FillStyle style = {};
  auto shader = paint.getShader();
  Color color = {};
  if (shader && shader->asColor(&color)) {
    color.alpha *= paint.getAlpha();
    style.color = color.premultiply();
    shader = nullptr;
  } else {
    style.color = paint.getColor().premultiply();
  }
  style.shader = shader;
  style.antiAlias = paint.isAntiAlias();
  style.colorFilter = paint.getColorFilter();
  style.maskFilter = paint.getMaskFilter();
  style.blendMode = paint.getBlendMode();
  return style;
}

static FillStyle CreateFillStyle(const Paint* paint) {
  return paint ? CreateFillStyle(*paint) : FillStyle();
}

void Canvas::drawLine(float x0, float y0, float x1, float y1, const Paint& paint) {
  Path path = {};
  path.moveTo(x0, y0);
  path.lineTo(x1, y1);
  auto realPaint = paint;
  realPaint.setStyle(PaintStyle::Stroke);
  drawPath(path, realPaint);
}

void Canvas::drawRect(const Rect& rect, const Paint& paint) {
  if (paint.getStroke()) {
    Path path = {};
    path.addRect(rect);
    drawPath(path, paint);
    return;
  }
  if (rect.isEmpty() || paint.nothingToDraw()) {
    return;
  }
  auto style = CreateFillStyle(paint);
  drawContext->drawRect(rect, *mcState, style);
}

void Canvas::drawOval(const Rect& oval, const Paint& paint) {
  RRect rRect = {};
  rRect.setOval(oval);
  drawRRect(rRect, paint);
}

void Canvas::drawCircle(float centerX, float centerY, float radius, const Paint& paint) {
  Rect rect =
      Rect::MakeLTRB(centerX - radius, centerY - radius, centerX + radius, centerY + radius);
  drawOval(rect, paint);
}

void Canvas::drawRoundRect(const Rect& rect, float radiusX, float radiusY, const Paint& paint) {
  RRect rRect = {};
  rRect.setRectXY(rect, radiusX, radiusY);
  drawRRect(rRect, paint);
}

void Canvas::drawRRect(const RRect& rRect, const Paint& paint) {
  if (rRect.radii.isZero()) {
    drawRect(rRect.rect, paint);
    return;
  }
  if (paint.getStroke()) {
    Path path = {};
    path.addRRect(rRect);
    drawPath(path, paint);
    return;
  }
  if (rRect.rect.isEmpty() || paint.nothingToDraw()) {
    return;
  }
  auto style = CreateFillStyle(paint);
  drawContext->drawRRect(rRect, *mcState, style);
}

void Canvas::drawPath(const Path& path, const Paint& paint) {
  if (path.isEmpty() || paint.nothingToDraw()) {
    return;
  }
  auto stroke = paint.getStroke();
  auto style = CreateFillStyle(paint);
  if (stroke && path.isLine()) {
    auto effect = PathEffect::MakeStroke(stroke);
    if (effect != nullptr) {
      auto fillPath = path;
      effect->applyTo(&fillPath);
      if (drawSimplePath(fillPath, style)) {
        return;
      }
    }
  }
  if (!stroke && drawSimplePath(path, style)) {
    return;
  }
  drawContext->drawPath(path, *mcState, style, stroke);
}

bool Canvas::drawSimplePath(const Path& path, const FillStyle& style) {
  Rect rect = {};
  if (path.isRect(&rect)) {
    drawContext->drawRect(rect, *mcState, style);
    return true;
  }
  RRect rRect;
  if (path.isOval(&rect)) {
    rRect.setOval(rect);
    drawContext->drawRRect(rRect, *mcState, style);
    return true;
  }
  if (path.isRRect(&rRect)) {
    drawContext->drawRRect(rRect, *mcState, style);
    return true;
  }
  return false;
}

static SamplingOptions GetDefaultSamplingOptions(std::shared_ptr<Image> image) {
  if (image == nullptr) {
    return {};
  }
  auto mipmapMode = image->hasMipmaps() ? MipmapMode::Linear : MipmapMode::None;
  return SamplingOptions(FilterMode::Linear, mipmapMode);
}

void Canvas::drawImage(std::shared_ptr<Image> image, float left, float top, const Paint* paint) {
  drawImage(std::move(image), Matrix::MakeTrans(left, top), paint);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Matrix& matrix, const Paint* paint) {
  auto sampling = GetDefaultSamplingOptions(image);
  drawImage(std::move(image), sampling, paint, &matrix);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Paint* paint) {
  auto sampling = GetDefaultSamplingOptions(image);
  drawImage(std::move(image), sampling, paint, nullptr);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                       const Paint* paint) {
  drawImage(std::move(image), sampling, paint, nullptr);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                       const Paint* paint, const Matrix* extraMatrix) {
  if (image == nullptr || (paint && paint->nothingToDraw())) {
    return;
  }
  auto state = *mcState;
  if (extraMatrix) {
    state.matrix.preConcat(*extraMatrix);
  }
  auto imageFilter = paint ? paint->getImageFilter() : nullptr;
  if (imageFilter != nullptr) {
    auto offset = Point::Zero();
    image = image->makeWithFilter(std::move(imageFilter), &offset);
    if (image == nullptr) {
      LOGE("Canvas::drawImage() Failed to apply filter to image!");
      return;
    }
    state.matrix.preTranslate(offset.x, offset.y);
  }
  auto rect = Rect::MakeWH(image->width(), image->height());
  auto style = CreateFillStyle(paint);
  drawContext->drawImageRect(std::move(image), sampling, rect, state, style);
}

void Canvas::drawSimpleText(const std::string& text, float x, float y, const Font& font,
                            const Paint& paint) {
  if (text.empty() || paint.nothingToDraw()) {
    return;
  }
  auto glyphRun = SimpleTextShaper::Shape(text, font);
  auto state = *mcState;
  state.matrix.preTranslate(x, y);
  auto style = CreateFillStyle(paint);
  drawContext->drawGlyphRun(std::move(glyphRun), state, style, paint.getStroke());
}

void Canvas::drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                        const Font& font, const Paint& paint) {
  if (glyphCount == 0 || paint.nothingToDraw()) {
    return;
  }
  GlyphRun glyphRun(font, {glyphs, glyphs + glyphCount}, {positions, positions + glyphCount});
  auto style = CreateFillStyle(paint);
  drawContext->drawGlyphRun(std::move(glyphRun), *mcState, style, paint.getStroke());
}

void Canvas::drawPicture(std::shared_ptr<Picture> picture) {
  drawContext->drawPicture(std::move(picture), *mcState);
}

void Canvas::drawPicture(std::shared_ptr<Picture> picture, const Matrix* matrix,
                         const Paint* paint) {
  if (picture == nullptr) {
    return;
  }
  auto state = *mcState;
  if (matrix) {
    state.matrix.preConcat(*matrix);
  }
  if (paint) {
    auto style = CreateFillStyle(*paint);
    drawLayer(std::move(picture), state, style, paint->getImageFilter());
  } else {
    drawContext->drawPicture(std::move(picture), state);
  }
}

void Canvas::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                       const FillStyle& style, std::shared_ptr<ImageFilter> filter) {
  if (picture->records.size() == 1) {
    LayerUnrollContext layerContext(drawContext, style, filter);
    picture->playback(&layerContext, state);
    if (layerContext.hasUnrolled()) {
      return;
    }
  }
  drawContext->drawLayer(std::move(picture), state, style, std::move(filter));
}

void Canvas::drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                       const Color colors[], size_t count, const SamplingOptions& sampling,
                       const Paint* paint) {
  // TODO: Support blend mode, atlas as source, colors as destination, colors can be nullptr.
  if (atlas == nullptr || count == 0 || (paint && paint->nothingToDraw())) {
    return;
  }
  auto style = CreateFillStyle(paint);
  auto state = *mcState;
  auto atlasRect = Rect::MakeWH(atlas->width(), atlas->height());
  for (size_t i = 0; i < count; ++i) {
    auto rect = tex[i];
    if (!rect.intersect(atlasRect)) {
      continue;
    }
    state.matrix = mcState->matrix * matrix[i];
    state.matrix.preTranslate(-rect.x(), -rect.y());
    auto glyphStyle = style;
    if (colors) {
      glyphStyle.color = colors[i].premultiply();
    }
    drawContext->drawImageRect(atlas, sampling, rect, state, glyphStyle);
  }
}
}  // namespace tgfx
