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
#include "core/RecordingContext.h"
#include "core/utils/Log.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
class AutoLayerForImageFilter {
 public:
  AutoLayerForImageFilter(Canvas* canvas, const Paint* paint) : canvas(canvas) {
    auto imageFilter = paint ? paint->getImageFilter() : nullptr;
    if (imageFilter != nullptr) {
      Paint layerPaint = {};
      layerPaint.setImageFilter(std::move(imageFilter));
      savedCount = canvas->saveLayer(&layerPaint);
    }
  }

  ~AutoLayerForImageFilter() {
    if (savedCount >= 0) {
      canvas->restoreToCount(savedCount);
    }
  }

 private:
  Canvas* canvas = nullptr;
  int savedCount = -1;
};

static FillStyle CreateFillStyle(const Paint& paint) {
  auto shader = paint.getShader();
  Color color = {};
  if (shader && shader->asColor(&color)) {
    color.alpha *= paint.getAlpha();
    shader = nullptr;
  } else {
    color = paint.getColor();
  }
  FillStyle style = {color, paint.getBlendMode()};
  style.shader = shader;
  style.antiAlias = paint.isAntiAlias();
  style.colorFilter = paint.getColorFilter();
  style.maskFilter = paint.getMaskFilter();
  return style;
}

static FillStyle CreateFillStyle(const Paint* paint) {
  return paint ? CreateFillStyle(*paint) : FillStyle();
}

Canvas::Canvas(DrawContext* drawContext, Surface* surface)
    : drawContext(drawContext), surface(surface) {
  mcState = std::make_unique<MCState>();
}

int Canvas::save() {
  stateStack.push(std::make_unique<CanvasState>(*mcState));
  return static_cast<int>(stateStack.size()) - 1;
}

int Canvas::saveLayer(const Paint* paint) {
  auto layer = std::make_unique<CanvasLayer>(drawContext, paint);
  drawContext = layer->layerContext.get();
  stateStack.push(std::make_unique<CanvasState>(*mcState, std::move(layer)));
  return static_cast<int>(stateStack.size()) - 1;
}

int Canvas::saveLayerAlpha(float alpha) {
  Paint paint = {};
  paint.setAlpha(alpha);
  return saveLayer(&paint);
}

void Canvas::restore() {
  if (stateStack.empty()) {
    return;
  }
  auto& canvasState = stateStack.top();
  *mcState = canvasState->mcState;
  auto layer = std::move(canvasState->savedLayer);
  stateStack.pop();
  if (layer != nullptr) {
    drawContext = layer->drawContext;
    auto layerContext = reinterpret_cast<RecordingContext*>(layer->layerContext.get());
    auto picture = layerContext->finishRecordingAsPicture();
    if (picture != nullptr) {
      auto style = CreateFillStyle(layer->layerPaint);
      drawLayer(picture, {}, style, layer->layerPaint.getImageFilter());
    }
  }
}

int Canvas::getSaveCount() const {
  return static_cast<int>(stateStack.size());
}

void Canvas::restoreToCount(int saveCount) {
  if (saveCount < 0) {
    saveCount = 0;
  }
  auto count = stateStack.size() - static_cast<size_t>(saveCount);
  for (size_t i = 0; i < count; i++) {
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

void Canvas::resetStateStack() {
  mcState = std::make_unique<MCState>();
  std::stack<std::unique_ptr<CanvasState>>().swap(stateStack);
}

void Canvas::clear(const Color& color) {
  drawClip({color, BlendMode::Src});
}

void Canvas::drawColor(const Color& color, BlendMode blendMode) {
  drawClip({color, blendMode});
}

void Canvas::drawPaint(const Paint& paint) {
  if (paint.nothingToDraw()) {
    return;
  }
  AutoLayerForImageFilter autoLayer(this, &paint);
  auto style = CreateFillStyle(paint);
  drawClip(style);
}

void Canvas::drawClip(const FillStyle& style) {
  auto& clip = mcState->clip;
  if (clip.isEmpty()) {
    if (clip.isInverseFillType()) {
      drawContext->drawStyle(*mcState, style);
    }
    return;
  }
  auto shape = Shape::MakeFrom(clip);
  DEBUG_ASSERT(shape != nullptr);
  auto fillStyle = style;
  if (fillStyle.shader) {
    fillStyle.shader = fillStyle.shader->makeWithMatrix(mcState->matrix);
  }
  if (fillStyle.maskFilter) {
    fillStyle.maskFilter = fillStyle.maskFilter->makeWithMatrix(mcState->matrix);
  }
  drawShape(std::move(shape), {}, fillStyle);
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
  AutoLayerForImageFilter autoLayer(this, &paint);
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
  auto& radii = rRect.radii;
  if (radii.x < 0.5f && radii.y < 0.5f) {
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
  AutoLayerForImageFilter autoLayer(this, &paint);
  auto style = CreateFillStyle(paint);
  drawContext->drawRRect(rRect, *mcState, style);
}

void Canvas::drawPath(const Path& path, const Paint& paint) {
  auto shape = Shape::MakeFrom(path);
  drawShape(std::move(shape), paint);
}

void Canvas::drawShape(std::shared_ptr<Shape> shape, const Paint& paint) {
  if (shape == nullptr || paint.nothingToDraw()) {
    return;
  }
  shape = Shape::ApplyStroke(std::move(shape), paint.getStroke());
  if (shape->isLine()) {
    // a line has no fill to draw.
    return;
  }
  AutoLayerForImageFilter autoLayer(this, &paint);
  auto style = CreateFillStyle(paint);
  Path path = {};
  if (shape->isSimplePath(&path) && path.isEmpty() && path.isInverseFillType()) {
    // No geometry to draw, so draw the clip instead.
    drawClip(style);
    return;
  }
  drawShape(std::move(shape), *mcState, style);
}

void Canvas::drawShape(std::shared_ptr<Shape> shape, const MCState& state, const FillStyle& style) {
  if (!shape->isInverseFillType()) {
    Rect rect = {};
    if (shape->isRect(&rect)) {
      drawContext->drawRect(rect, state, style);
      return;
    }
    RRect rRect = {};
    if (shape->isOval(&rect)) {
      rRect.setOval(rect);
      drawContext->drawRRect(rRect, state, style);
      return;
    }
    if (shape->isRRect(&rRect)) {
      drawContext->drawRRect(rRect, state, style);
      return;
    }
  }
  drawContext->drawShape(std::move(shape), state, style);
}

static SamplingOptions GetDefaultSamplingOptions(Image* image) {
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
  auto sampling = GetDefaultSamplingOptions(image.get());
  drawImage(std::move(image), sampling, paint, &matrix);
}

void Canvas::drawImage(std::shared_ptr<Image> image, const Paint* paint) {
  auto sampling = GetDefaultSamplingOptions(image.get());
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
  auto style = CreateFillStyle(paint);
  if (!image->isAlphaOnly()) {
    style.shader = nullptr;
  }
  drawContext->drawImage(std::move(image), sampling, state, style);
}

void Canvas::drawSimpleText(const std::string& text, float x, float y, const Font& font,
                            const Paint& paint) {
  if (text.empty()) {
    return;
  }
  auto textBlob = TextBlob::MakeFrom(text, font);
  drawTextBlob(std::move(textBlob), x, y, paint);
}

void Canvas::drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                        const Font& font, const Paint& paint) {
  drawGlyphs(glyphs, positions, glyphCount, GlyphFace::Wrap(font), paint);
}

void Canvas::drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                        std::shared_ptr<GlyphFace> glyphFace, const Paint& paint) {
  if (glyphCount == 0 || glyphFace == nullptr || paint.nothingToDraw()) {
    return;
  }
  AutoLayerForImageFilter autoLayer(this, &paint);
  GlyphRun glyphRun(glyphFace, {glyphs, glyphs + glyphCount}, {positions, positions + glyphCount});
  auto glyphRunList = std::make_shared<GlyphRunList>(std::move(glyphRun));
  auto style = CreateFillStyle(paint);
  drawContext->drawGlyphRunList(std::move(glyphRunList), paint.getStroke(), *mcState, style);
}

void Canvas::drawTextBlob(std::shared_ptr<TextBlob> textBlob, float x, float y,
                          const Paint& paint) {
  if (textBlob == nullptr || paint.nothingToDraw()) {
    return;
  }
  AutoLayerForImageFilter autoLayer(this, &paint);
  auto state = *mcState;
  state.matrix.preTranslate(x, y);
  auto style = CreateFillStyle(paint);
  for (auto& glyphRunList : textBlob->glyphRunLists) {
    drawContext->drawGlyphRunList(glyphRunList, paint.getStroke(), state, style);
  }
}

void Canvas::drawPicture(std::shared_ptr<Picture> picture) {
  if (picture == nullptr) {
    return;
  }
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
    style.shader = nullptr;
    drawLayer(std::move(picture), state, style, paint->getImageFilter());
  } else {
    drawContext->drawPicture(std::move(picture), state);
  }
}

void Canvas::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                       const FillStyle& style, std::shared_ptr<ImageFilter> imageFilter) {
  DEBUG_ASSERT(style.shader == nullptr);
  if (imageFilter != nullptr) {
    Point offset = {};
    if (auto image = picture->asImage(&offset)) {
      Point filterOffset = {};
      image = image->makeWithFilter(std::move(imageFilter), &filterOffset);
      if (image == nullptr) {
        LOGE("Canvas::drawLayer() Failed to apply filter to image!");
        return;
      }
      auto drawState = state;
      drawState.matrix.preTranslate(offset.x + filterOffset.x, offset.y + filterOffset.y);
      drawContext->drawImage(std::move(image), {}, drawState, style);
      return;
    }
  } else if (picture->records.size() == 1 && style.maskFilter == nullptr) {
    LayerUnrollContext layerContext(drawContext, style);
    picture->playback(&layerContext, state);
    if (layerContext.hasUnrolled()) {
      return;
    }
  }
  drawContext->drawLayer(std::move(picture), std::move(imageFilter), state, style);
}

void Canvas::drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                       const Color colors[], size_t count, const SamplingOptions& sampling,
                       const Paint* paint) {
  // TODO: Support blend mode, atlas as source, colors as destination, colors can be nullptr.
  if (atlas == nullptr || count == 0 || (paint && paint->nothingToDraw())) {
    return;
  }
  AutoLayerForImageFilter autoLayer(this, paint);
  auto style = CreateFillStyle(paint);
  if (!atlas->isAlphaOnly()) {
    style.shader = nullptr;
  }
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
      glyphStyle.color = colors[i];
    }
    if (rect == atlasRect) {
      drawContext->drawImage(atlas, sampling, state, glyphStyle);
    } else {
      drawContext->drawImageRect(atlas, rect, sampling, state, glyphStyle);
    }
  }
}
}  // namespace tgfx
