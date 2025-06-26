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
#include "core/RecordingContext.h"
#include "core/shapes/StrokeShape.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "images/SubsetImage.h"
#include "shapes/MatrixShape.h"
#include "shapes/PathShape.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
class AutoLayerForImageFilter {
 public:
  AutoLayerForImageFilter(Canvas* canvas, std::shared_ptr<ImageFilter> imageFilter)
      : canvas(canvas) {
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

#define SaveLayerForImageFilter(imageFilter)                                        \
  std::unique_ptr<AutoLayerForImageFilter> autoLayer = nullptr;                     \
  if (auto filter = imageFilter) {                                                  \
    autoLayer = std::make_unique<AutoLayerForImageFilter>(this, std::move(filter)); \
  }

static Fill GetFillStyleForImage(const Paint* paint, const Image* image) {
  if (paint == nullptr) {
    return {};
  }
  auto fill = paint->getFill();
  if (!image->isAlphaOnly()) {
    fill.shader = nullptr;
  }
  return fill;
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
      drawLayer(std::move(picture), {}, layer->layerPaint.getFill(),
                layer->layerPaint.getImageFilter());
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
  drawColor(color, BlendMode::Src);
}

void Canvas::drawColor(const Color& color, BlendMode blendMode) {
  drawFill(*mcState, {color, blendMode});
}

void Canvas::drawPaint(const Paint& paint) {
  SaveLayerForImageFilter(paint.getImageFilter());
  drawFill(*mcState, paint.getFill());
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
  if (rect.isEmpty()) {
    return;
  }
  SaveLayerForImageFilter(paint.getImageFilter());
  drawContext->drawRect(rect, *mcState, paint.getFill());
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

static bool UseDrawPath(const Paint& paint, const Point& radii, const Matrix& viewMatrix) {
  auto stroke = paint.getStroke();
  if (!stroke) {
    return false;
  }
  if (!viewMatrix.rectStaysRect()) {
    return false;
  }
  float xRadius = std::fabs(viewMatrix.getScaleX() * radii.x + viewMatrix.getSkewY() * radii.y);
  float yRadius = std::fabs(viewMatrix.getSkewX() * radii.x + viewMatrix.getScaleY() * radii.y);
  Point scaledStroke = {};
  scaledStroke.x = std::fabs(stroke->width * (viewMatrix.getScaleX() + viewMatrix.getSkewY()));
  scaledStroke.y = std::fabs(stroke->width * (viewMatrix.getSkewX() + viewMatrix.getScaleY()));

  // Half of strokewidth is greater than radius
  if (scaledStroke.x * 0.5f > xRadius || scaledStroke.y * 0.5f > yRadius) {
    return true;
  }
  // Handle thick strokes for near-circular ellipses
  if (stroke->width > 1.0f && (radii.x * 0.5f > radii.y || radii.y * 0.5f > radii.x)) {
    return true;
  }
  // The matrix may have a rotation by an odd multiple of 90 degrees.
  if (viewMatrix.getScaleX() == 0) {
    std::swap(xRadius, yRadius);
    std::swap(scaledStroke.x, scaledStroke.y);
  }

  if (FloatNearlyZero(scaledStroke.length())) {
    scaledStroke.set(0.5f, 0.5f);
  } else {
    scaledStroke *= 0.5f;
  }

  // Handle thick strokes for near-circular ellipses
  if (scaledStroke.length() > 0.5f && (0.5f * xRadius > yRadius || 0.5f * yRadius > xRadius)) {
    return true;
  }

  // Curvature of the stroke is less than curvature of the ellipse
  if (scaledStroke.x * radii.y * radii.y < scaledStroke.y * scaledStroke.y * radii.x) {
    return true;
  }
  if (scaledStroke.y * radii.x * radii.x < scaledStroke.x * scaledStroke.x * radii.y) {
    return true;
  }
  return false;
}

void Canvas::drawRRect(const RRect& rRect, const Paint& paint) {
  auto& radii = rRect.radii;
  if (radii.x < 0.5f && radii.y < 0.5f) {
    drawRect(rRect.rect, paint);
    return;
  }
  if (UseDrawPath(paint, radii, mcState->matrix)) {
    Path path = {};
    path.addRRect(rRect);
    drawPath(path, paint);
    return;
  }
  if (rRect.rect.isEmpty()) {
    return;
  }
  SaveLayerForImageFilter(paint.getImageFilter());
  drawContext->drawRRect(rRect, *mcState, paint.getFill(), paint.getStroke());
}

void Canvas::drawPath(const Path& path, const Paint& paint) {
  SaveLayerForImageFilter(paint.getImageFilter());
  drawPath(path, *mcState, paint.getFill(), paint.getStroke());
}

/// Check if the line is axis-aligned and convert it to a rect
static bool StrokeLineIsRect(const Stroke& stroke, const Point line[2], Rect* rect) {
  if (stroke.cap == LineCap::Round) {
    return false;
  }
  // check if the line is axis-aligned
  if (line[0].x != line[1].x && line[0].y != line[1].y) {
    return false;
  }
  // use the stroke width and line cap to convert the line to a rect
  auto left = std::min(line[0].x, line[1].x);
  auto top = std::min(line[0].y, line[1].y);
  auto right = std::max(line[0].x, line[1].x);
  auto bottom = std::max(line[0].y, line[1].y);
  auto halfWidth = stroke.width / 2.0f;
  if (stroke.cap == LineCap::Square) {
    if (rect) {
      rect->setLTRB(left - halfWidth, top - halfWidth, right + halfWidth, bottom + halfWidth);
    }
    return true;
  }
  if (rect) {
    if (left == right) {
      rect->setLTRB(left - halfWidth, top, right + halfWidth, bottom);
    } else {
      rect->setLTRB(left, top - halfWidth, right, bottom + halfWidth);
    }
  }
  return true;
}

void Canvas::drawPath(const Path& path, const MCState& state, const Fill& fill,
                      const Stroke* stroke) const {
  if (path.isEmpty()) {
    if (path.isInverseFillType()) {
      // No geometry to draw, so draw the fill instead.
      drawFill(state, fill);
    }
    return;
  }
  Rect rect = {};
  Point line[2] = {};
  if (path.isLine(line)) {
    if (!stroke) {
      // a line has no fill to draw.
      return;
    }
    if (StrokeLineIsRect(*stroke, line, &rect)) {
      drawContext->drawRect(rect, state, fill);
      return;
    }
  }
  if (stroke == nullptr) {
    if (path.isRect(&rect)) {
      drawContext->drawRect(rect, state, fill);
      return;
    }
    RRect rRect = {};
    if (path.isOval(&rect)) {
      rRect.setOval(rect);
      drawContext->drawRRect(rRect, state, fill, stroke);
      return;
    }
    if (path.isRRect(&rRect)) {
      drawContext->drawRRect(rRect, state, fill, stroke);
      return;
    }
    drawContext->drawPath(path, state, fill);
  } else {
    auto shape = Shape::MakeFrom(path);
    if (shape == nullptr) {
      return;
    }
    shape = StrokeShape::Apply(std::move(shape), stroke, false);
    if (shape == nullptr) {
      return;
    }
    drawContext->drawShape(shape, state, fill);
  }
}

void Canvas::drawShape(std::shared_ptr<Shape> shape, const Paint& paint) {
  if (shape == nullptr) {
    return;
  }
  SaveLayerForImageFilter(paint.getImageFilter());
  auto fill = paint.getFill();
  auto state = *mcState;
  auto stroke = paint.getStroke();
  Path* path = nullptr;
  if (shape->type() == Shape::Type::Path) {
    path = &std::static_pointer_cast<PathShape>(shape)->path;
  } else if (stroke == nullptr && shape->type() == Shape::Type::Matrix) {
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    if (matrixShape->shape->isSimplePath()) {
      Matrix inverse = {};
      if (matrixShape->matrix.invert(&inverse)) {
        path = &std::static_pointer_cast<PathShape>(matrixShape->shape)->path;
        state.matrix.preConcat(matrixShape->matrix);
        fill = fill.makeWithMatrix(inverse);
      }
    }
  }
  if (path) {
    drawPath(*path, state, fill, stroke);
    return;
  }
  shape = StrokeShape::Apply(std::move(shape), stroke, false);
  if (shape != nullptr) {
    drawContext->drawShape(std::move(shape), state, fill);
  }
}

void Canvas::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                       const Paint* paint) {
  if (image == nullptr) {
    return;
  }
  SaveLayerForImageFilter(paint ? paint->getImageFilter() : nullptr);
  auto srcRect = Rect::MakeWH(image->width(), image->height());
  auto fill = GetFillStyleForImage(paint, image.get());
  drawImageRect(std::move(image), srcRect, sampling, fill);
}

void Canvas::drawImage(std::shared_ptr<Image> image, float left, float top,
                       const SamplingOptions& sampling, const Paint* paint) {
  if (image == nullptr) {
    return;
  }
  SaveLayerForImageFilter(paint ? paint->getImageFilter() : nullptr);
  auto srcRect = Rect::MakeWH(image->width(), image->height());
  auto fill = GetFillStyleForImage(paint, image.get());
  auto dstMatrix = Matrix::MakeTrans(left, top);
  drawImageRect(std::move(image), srcRect, sampling, fill, &dstMatrix);
}

void Canvas::drawImageRect(std::shared_ptr<Image> image, const Rect& dstRect,
                           const SamplingOptions& sampling, const Paint* paint) {
  if (image == nullptr) {
    return;
  }
  auto srcRect = Rect::MakeWH(image->width(), image->height());
  drawImageRect(std::move(image), srcRect, dstRect, sampling, paint);
}

void Canvas::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                           const SamplingOptions& sampling, const Paint* paint,
                           SrcRectConstraint constraint) {
  if (image == nullptr || srcRect.isEmpty() || dstRect.isEmpty()) {
    return;
  }
  SaveLayerForImageFilter(paint ? paint->getImageFilter() : nullptr);
  auto fill = GetFillStyleForImage(paint, image.get());
  if (srcRect == dstRect) {
    drawImageRect(std::move(image), srcRect, sampling, fill, nullptr, constraint);
    return;
  }
  auto dstMatrix = Matrix::MakeTrans(-srcRect.left, -srcRect.top);
  auto scaleX = dstRect.width() / srcRect.width();
  auto scaleY = dstRect.height() / srcRect.height();
  dstMatrix.postScale(scaleX, scaleY);
  dstMatrix.postTranslate(dstRect.left, dstRect.top);
  drawImageRect(std::move(image), srcRect, sampling, fill, &dstMatrix, constraint);
}

// Fills a rectangle with the given Image. The image is sampled from the area defined by rect,
// and rendered into the same area, transformed by extraMatrix and the current matrix.
void Canvas::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                           const SamplingOptions& sampling, const Fill& fill,
                           const Matrix* dstMatrix, SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(!rect.isEmpty());
  auto type = Types::Get(image.get());
  if (type != Types::ImageType::Subset && dstMatrix == nullptr) {
    drawContext->drawImageRect(std::move(image), rect, sampling, *mcState, fill, constraint);
    return;
  }
  auto viewMatrix = dstMatrix ? *dstMatrix : Matrix::I();
  auto imageRect = rect;
  if (type == Types::ImageType::Subset) {
    auto safeBounds = Rect::MakeWH(image->width(), image->height());
    safeBounds.inset(0.5f, 0.5f);
    if (constraint == SrcRectConstraint::Strict || safeBounds.contains(rect)) {
      // Unwrap the subset image to maximize the merging of draw calls.
      auto subsetImage = static_cast<const SubsetImage*>(image.get());
      auto& subset = subsetImage->bounds;
      imageRect.offset(subset.left, subset.top);
      viewMatrix.preTranslate(-subset.left, -subset.top);
      image = subsetImage->source;
    }
  }
  Matrix fillMatrix = {};
  if (!viewMatrix.invert(&fillMatrix)) {
    return;
  }
  auto imageState = *mcState;
  imageState.matrix.preConcat(viewMatrix);
  auto imageFill = fill.makeWithMatrix(fillMatrix);
  drawContext->drawImageRect(std::move(image), imageRect, sampling, imageState, imageFill,
                             constraint);
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

  if (glyphCount == 0) {
    return;
  }
  SaveLayerForImageFilter(paint.getImageFilter());
  GlyphRun glyphRun(font, {glyphs, glyphs + glyphCount}, {positions, positions + glyphCount});
  auto glyphRunList = std::make_shared<GlyphRunList>(std::move(glyphRun));
  drawContext->drawGlyphRunList(std::move(glyphRunList), *mcState, paint.getFill(),
                                paint.getStroke());
}

void Canvas::drawTextBlob(std::shared_ptr<TextBlob> textBlob, float x, float y,
                          const Paint& paint) {
  if (textBlob == nullptr) {
    return;
  }
  SaveLayerForImageFilter(paint.getImageFilter());
  auto state = *mcState;
  state.matrix.preTranslate(x, y);
  auto stroke = paint.getStroke();
  for (auto& glyphRunList : textBlob->glyphRunLists) {
    drawContext->drawGlyphRunList(glyphRunList, state, paint.getFill(), stroke);
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
    auto fill = paint->getFill();
    fill.shader = nullptr;
    drawLayer(std::move(picture), state, fill, paint->getImageFilter());
  } else {
    drawContext->drawPicture(std::move(picture), state);
  }
}

class LayerUnrollModifier : public FillModifier {
 public:
  explicit LayerUnrollModifier(Fill layerFill) : layerFill(std::move(layerFill)) {
  }

  Fill transform(const Fill& fill) const override {
    auto newFill = fill;
    newFill.color.alpha *= layerFill.color.alpha;
    newFill.blendMode = layerFill.blendMode;
    newFill.colorFilter = ColorFilter::Compose(fill.colorFilter, layerFill.colorFilter);
    return newFill;
  }

 private:
  Fill layerFill = {};
};

void Canvas::drawLayer(std::shared_ptr<Picture> picture, const MCState& state, const Fill& fill,
                       std::shared_ptr<ImageFilter> imageFilter) {
  DEBUG_ASSERT(fill.shader == nullptr);
  DEBUG_ASSERT(picture != nullptr);
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
      auto fillMatrix = Matrix::MakeTrans(-offset.x - filterOffset.x, -offset.y - +filterOffset.y);
      auto imageRect = Rect::MakeWH(image->width(), image->height());
      drawContext->drawImageRect(std::move(image), imageRect, {}, drawState,
                                 fill.makeWithMatrix(fillMatrix), SrcRectConstraint::Fast);
      return;
    }
  } else if (picture->drawCount == 1 && fill.maskFilter == nullptr) {
    LayerUnrollModifier unrollModifier(fill);
    picture->playback(drawContext, state, &unrollModifier);
    return;
  }
  drawContext->drawLayer(std::move(picture), std::move(imageFilter), state, fill);
}

void Canvas::drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                       const Color colors[], size_t count, const SamplingOptions& sampling,
                       const Paint* paint) {
  // TODO: Support blend mode, atlas as source, colors as destination, colors can be nullptr.
  if (atlas == nullptr || count == 0) {
    return;
  }
  auto fill = GetFillStyleForImage(paint, atlas.get());
  SaveLayerForImageFilter(paint ? paint->getImageFilter() : nullptr);
  auto atlasRect = Rect::MakeWH(atlas->width(), atlas->height());
  for (size_t i = 0; i < count; ++i) {
    auto rect = tex[i];
    if (!rect.intersect(atlasRect)) {
      continue;
    }
    auto dstMatrix = matrix[i];
    dstMatrix.preTranslate(-rect.x(), -rect.y());
    auto glyphFill = fill;
    if (colors) {
      glyphFill.color = colors[i];
    }
    drawImageRect(atlas, rect, sampling, glyphFill, &dstMatrix);
  }
}

void Canvas::drawFill(const MCState& state, const Fill& fill) const {
  if (state.clip.isEmpty()) {
    if (!state.clip.isInverseFillType()) {
      return;
    }
    drawContext->drawFill(fill.makeWithMatrix(state.matrix));
  } else {
    drawPath(state.clip, {}, fill.makeWithMatrix(state.matrix), nullptr);
  }
}

}  // namespace tgfx
