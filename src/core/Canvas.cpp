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
#include "core/FillStyle.h"
#include "core/PathRef.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/FillRectOp.h"
#include "tgfx/core/PathEffect.h"
#include "tgfx/gpu/Surface.h"
#include "utils/SimpleTextShaper.h"
#include "utils/StrokeKey.h"

namespace tgfx {
Canvas::Canvas(const Path& initClip) {
  state.clip = initClip;
}

void Canvas::save() {
  onSave();
}

void Canvas::restore() {
  onRestore();
}

size_t Canvas::getSaveCount() const {
  return stack.size();
}

void Canvas::restoreToCount(size_t saveCount) {
  auto n = stack.size() - saveCount;
  for (size_t i = 0; i < n; i++) {
    restore();
  }
}

void Canvas::translate(float dx, float dy) {
  if (dx == 0 && dy == 0) {
    return;
  }
  onTranslate(dx, dy);
}

void Canvas::scale(float sx, float sy) {
  if (sx == 1.0f && sy == 1.0f) {
    return;
  }
  onScale(sx, sy);
}

void Canvas::rotate(float degrees) {
  if (fmodf(degrees, 360.0f) == 0) {
    return;
  }
  onRotate(degrees);
}

void Canvas::rotate(float degrees, float px, float py) {
  if (fmodf(degrees, 360.0f) == 0) {
    return;
  }
  Matrix m = {};
  m.setRotate(degrees, px, py);
  state.matrix.preConcat(m);
  onConcat(m);
}

void Canvas::skew(float sx, float sy) {
  if (sx == 0 && sy == 0) {
    return;
  }
  onSkew(sx, sy);
}

void Canvas::concat(const Matrix& matrix) {
  if (matrix.isIdentity()) {
    return;
  }
  onConcat(matrix);
}

const Matrix& Canvas::getMatrix() const {
  return state.matrix;
}

void Canvas::setMatrix(const Matrix& matrix) {
  onSetMatrix(matrix);
}

void Canvas::resetMatrix() {
  onResetMatrix();
}

const Path& Canvas::getTotalClip() const {
  return state.clip;
}

void Canvas::clipRect(const Rect& rect) {
  onClipRect(rect);
}

void Canvas::clipPath(const Path& path) {
  onClipPath(path);
}

void Canvas::clear() {
  onClear();
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
  onDrawRect(rect, style);
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
  onDrawRRect(rRect, style);
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
  onDrawPath(path, style, stroke);
}

bool Canvas::drawSimplePath(const Path& path, const FillStyle& style) {
  Rect rect = {};
  if (path.asRect(&rect)) {
    onDrawRect(rect, style);
    return true;
  }
  RRect rRect;
  if (path.asRRect(&rRect)) {
    onDrawRRect(rRect, style);
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
  auto matrix = extraMatrix ? *extraMatrix : Matrix::I();
  auto imageFilter = paint ? paint->getImageFilter() : nullptr;
  if (imageFilter != nullptr) {
    auto offset = Point::Zero();
    image = image->makeWithFilter(std::move(imageFilter), &offset);
    if (image == nullptr) {
      return;
    }
    matrix.preTranslate(offset.x, offset.y);
  }
  auto rect = Rect::MakeWH(image->width(), image->height());
  auto style = CreateFillStyle(paint);
  drawImageRect(rect, std::move(image), sampling, style, matrix);
}

void Canvas::drawImageRect(const Rect& rect, std::shared_ptr<Image> image,
                           const SamplingOptions& sampling, const FillStyle& style,
                           const Matrix& extraMatrix) {
  auto hasExtraMatrix = !extraMatrix.isIdentity();
  if (hasExtraMatrix) {
    save();
    onConcat(extraMatrix);
  }
  onDrawImageRect(rect, std::move(image), sampling, style);
  if (hasExtraMatrix) {
    restore();
  }
}

void Canvas::drawSimpleText(const std::string& text, float x, float y, const Font& font,
                            const Paint& paint) {
  if (text.empty() || paint.nothingToDraw()) {
    return;
  }
  auto glyphRun = SimpleTextShaper::Shape(text, font);
  if (x != 0 || y != 0) {
    save();
    translate(x, y);
  }
  auto style = CreateFillStyle(paint);
  onDrawGlyphRun(std::move(glyphRun), style, paint.getStroke());
  if (x != 0 || y != 0) {
    restore();
  }
}

void Canvas::drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                        const Font& font, const Paint& paint) {
  if (glyphCount == 0 || paint.nothingToDraw()) {
    return;
  }
  GlyphRun glyphRun(font, {glyphs, glyphs + glyphCount}, {positions, positions + glyphCount});
  auto style = CreateFillStyle(paint);
  onDrawGlyphRun(std::move(glyphRun), style, paint.getStroke());
}

void Canvas::drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                       const Color colors[], size_t count, const SamplingOptions& sampling,
                       const Paint* paint) {
  // TODO: Support blend mode, atlas as source, colors as destination, colors can be nullptr.
  if (atlas == nullptr || count == 0 || (paint && paint->nothingToDraw())) {
    return;
  }
  auto style = CreateFillStyle(paint);
  for (size_t i = 0; i < count; ++i) {
    auto rect = tex[i];
    auto glyphMatrix = matrix[i];
    glyphMatrix.preTranslate(-rect.x(), -rect.y());
    auto glyphStyle = style;
    if (colors) {
      glyphStyle.color = colors[i].premultiply();
    }
    drawImageRect(rect, atlas, sampling, glyphStyle, glyphMatrix);
  }
}

void Canvas::resetMCState(const Path& initClip) {
  state = {};
  state.clip = initClip;
  std::stack<MCState>().swap(stack);
}

void Canvas::onSave() {
  stack.push(state);
}

bool Canvas::onRestore() {
  if (stack.empty()) {
    return false;
  }
  state = stack.top();
  stack.pop();
  return true;
}

void Canvas::onTranslate(float dx, float dy) {
  state.matrix.preTranslate(dx, dy);
}

void Canvas::onScale(float sx, float sy) {
  state.matrix.preScale(sx, sy);
}

void Canvas::onRotate(float degrees) {
  state.matrix.preRotate(degrees);
}

void Canvas::onSkew(float sx, float sy) {
  state.matrix.preSkew(sx, sy);
}

void Canvas::onConcat(const Matrix& matrix) {
  state.matrix.preConcat(matrix);
}

void Canvas::onSetMatrix(const Matrix& matrix) {
  state.matrix = matrix;
}

void Canvas::onResetMatrix() {
  state.matrix.reset();
}

void Canvas::onClipRect(const Rect& rect) {
  Path path = {};
  path.addRect(rect);
  path.transform(state.matrix);
  state.clip.addPath(path, PathOp::Intersect);
}

void Canvas::onClipPath(const Path& path) {
  auto clipPath = path;
  clipPath.transform(state.matrix);
  state.clip.addPath(clipPath, PathOp::Intersect);
}
}  // namespace tgfx
