/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/layers/LayerRecorder.h"
#include "core/shapes/MatrixShape.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/ShapeUtils.h"
#include "core/utils/StrokeUtils.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/GeometryContent.h"
#include "layers/contents/MatrixContent.h"
#include "layers/contents/MeshContent.h"
#include "layers/contents/PathContent.h"
#include "layers/contents/RRectContent.h"
#include "layers/contents/RRectsContent.h"
#include "layers/contents/RectContent.h"
#include "layers/contents/RectsContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/TextContent.h"

namespace tgfx {

// A stroke style is "complex" when it carries a non-Center alignment or a path effect (e.g. dash).
// Complex styles cannot be resolved by a plain Canvas stroke call and must be expanded into fill
// geometry downstream (ShapeContent handles the clip-mask expansion and effect application). An
// entry no paint captured is never complex.
static bool ComplexStrokeStyle(const LayerPaint* paint) {
  return paint != nullptr && paint->style == PaintStyle::Stroke &&
         (paint->strokeAlign != StrokeAlign::Center || paint->pathEffect != nullptr);
}

LayerRecorder::LayerRecorder() = default;

LayerRecorder::~LayerRecorder() = default;

void LayerRecorder::addRect(const Rect& rect, const LayerPaint* paint) {
  addRect(rect, paint, _matrix);
}

void LayerRecorder::addRRect(const RRect& rRect, const LayerPaint* paint) {
  addRRect(rRect, paint, _matrix);
}

void LayerRecorder::addPath(const Path& path, const LayerPaint* paint) {
  addPath(path, paint, _matrix);
}

void LayerRecorder::addShape(std::shared_ptr<Shape> shape, const LayerPaint* paint) {
  // The paint is copied by the private overload, never referenced, so the caller's paint may be
  // released as soon as this call returns.
  addShape(std::move(shape), paint, _matrix);
}

void LayerRecorder::addTextBlob(std::shared_ptr<TextBlob> textBlob, const LayerPaint* paint,
                                float x, float y) {
  if (textBlob == nullptr) {
    return;
  }
  // TextContent does not consume strokeAlign or pathEffect. Callers that need Inside/Outside
  // alignment or a path effect on text must first convert the TextBlob into a Shape (see
  // StrokeStyle::prepareGlyphRun) and route it through addShape().
  DEBUG_ASSERT(!ComplexStrokeStyle(paint));
  flushPending();
  auto& list = (paint != nullptr ? paint->placement : LayerPlacement::Background) ==
                       LayerPlacement::Foreground
                   ? foregrounds
                   : contents;
  // The offset only affects the text position, while _matrix affects both
  // the text position and the paint (shader/maskFilter coordinates).
  auto offset = Point::Make(x, y);
  std::unique_ptr<GeometryContent> content =
      std::make_unique<TextContent>(std::move(textBlob), offset, paint);
  if (!_matrix.isIdentity()) {
    content = std::make_unique<MatrixContent>(std::move(content), _matrix);
  }
  list.push_back(std::move(content));
}

void LayerRecorder::addMesh(std::shared_ptr<Mesh> mesh, const LayerPaint* paint) {
  if (mesh == nullptr) {
    return;
  }
  // MeshContent does not consume strokeAlign or pathEffect. There is no reasonable way to apply
  // stroke alignment or a path effect to a Mesh, so reject the paint here.
  DEBUG_ASSERT(!ComplexStrokeStyle(paint));
  flushPending();
  auto& list = (paint != nullptr ? paint->placement : LayerPlacement::Background) ==
                       LayerPlacement::Foreground
                   ? foregrounds
                   : contents;
  std::unique_ptr<GeometryContent> content = std::make_unique<MeshContent>(std::move(mesh), paint);
  if (!_matrix.isIdentity()) {
    content = std::make_unique<MatrixContent>(std::move(content), _matrix);
  }
  list.push_back(std::move(content));
}

const Matrix& LayerRecorder::getMatrix() const {
  return _matrix;
}

void LayerRecorder::setMatrix(const Matrix& matrix) {
  _matrix = matrix;
}

void LayerRecorder::resetMatrix() {
  _matrix = Matrix::I();
}

void LayerRecorder::addRect(const Rect& rect, const LayerPaint* paint, const Matrix& matrix) {
  if (rect.isEmpty()) {
    return;
  }
  if (ComplexStrokeStyle(paint)) {
    Path path = {};
    path.addRect(rect);
    flushPending(PendingType::Shape, paint, matrix);
    pendingShape = Shape::MakeFrom(std::move(path));
    return;
  }
  if (!canAppend(PendingType::Rect, paint, matrix)) {
    flushPending(PendingType::Rect, paint, matrix);
  }
  pendingRects.push_back(rect);
}

void LayerRecorder::addRRect(const RRect& rRect, const LayerPaint* paint, const Matrix& matrix) {
  if (rRect.rect().isEmpty()) {
    return;
  }
  if (rRect.isRect()) {
    addRect(rRect.rect(), paint, matrix);
    return;
  }
  if (ComplexStrokeStyle(paint)) {
    Path path = {};
    path.addRRect(rRect);
    flushPending(PendingType::Shape, paint, matrix);
    pendingShape = Shape::MakeFrom(std::move(path));
    return;
  }
  if (!canAppend(PendingType::RRect, paint, matrix)) {
    flushPending(PendingType::RRect, paint, matrix);
  }
  pendingRRects.push_back(rRect);
}

void LayerRecorder::addPath(const Path& path, const LayerPaint* paint, const Matrix& matrix) {
  if (path.isEmpty()) {
    return;
  }
  if (!ComplexStrokeStyle(paint) && tryAddSimplifiedPath(path, paint, matrix)) {
    return;
  }
  flushPending(PendingType::Shape, paint, matrix);
  pendingShape = Shape::MakeFrom(path);
}

void LayerRecorder::addShape(std::shared_ptr<Shape> shape, const LayerPaint* paint,
                             const Matrix& matrix) {
  if (shape == nullptr) {
    return;
  }
  if (ComplexStrokeStyle(paint)) {
    flushPending(PendingType::Shape, paint, matrix);
    pendingShape = std::move(shape);
    return;
  }
  if (shape->isSimplePath()) {
    addPath(shape->getPath(), paint, matrix);
    return;
  }
  if (tryAddSimplifiedMatrixShape(shape, paint, matrix)) {
    return;
  }
  flushPending(PendingType::Shape, paint, matrix);
  pendingShape = std::move(shape);
}

bool LayerRecorder::tryAddSimplifiedMatrixShape(const std::shared_ptr<Shape>& shape,
                                                const LayerPaint* paint, const Matrix& matrix) {
  auto matrixShape = ShapeUtils::AsMatrixShape(shape.get());
  if (matrixShape == nullptr || !matrixShape->shape->isSimplePath()) {
    return false;
  }
  // Pulling matrixShape->matrix out of the shape and folding it into the combined CTM affects how
  // the paint is sampled. The shader is supplied in the original shape's local space, so apply the
  // inverse to keep its appearance unchanged. The stroke is also specified in that local space,
  // and uniform scales need to be canceled out so the visual stroke width stays as the caller
  // intended; non-uniform scales would distort the stroke and are not safe to fold into the CTM.
  Matrix inverseInner = {};
  if (!matrixShape->matrix.invert(&inverseInner)) {
    return false;
  }
  auto combinedMatrix = matrixShape->matrix;
  combinedMatrix.postConcat(matrix);
  if (paint == nullptr) {
    // Nothing was painted, so there is no shader or stroke width to keep in the original space.
    return tryAddSimplifiedPath(matrixShape->shape->getPath(), nullptr, combinedMatrix);
  }
  LayerPaint adjustedPaint = *paint;
  if (adjustedPaint.shader != nullptr) {
    adjustedPaint.shader = adjustedPaint.shader->makeWithMatrix(inverseInner);
  }
  if (adjustedPaint.style == PaintStyle::Stroke) {
    auto scales = matrixShape->matrix.getAxisScales();
    if (!FloatNearlyEqual(scales.x, scales.y)) {
      return false;
    }
    if (!FloatNearlyEqual(scales.x, 1.0f)) {
      DEBUG_ASSERT(scales.x != 0);
      adjustedPaint.stroke.width = paint->stroke.width / scales.x;
    }
  }
  return tryAddSimplifiedPath(matrixShape->shape->getPath(), &adjustedPaint, combinedMatrix);
}

bool LayerRecorder::tryAddSimplifiedPath(const Path& path, const LayerPaint* paint,
                                         const Matrix& matrix) {
  Point line[2] = {};
  if (path.isLine(line)) {
    if (paint == nullptr || paint->style != PaintStyle::Stroke) {
      return true;
    }
    Rect rect = {};
    if (StrokeLineToRect(paint->stroke, line, &rect)) {
      LayerPaint fillPaint = *paint;
      fillPaint.style = PaintStyle::Fill;
      addRect(rect, &fillPaint, matrix);
      return true;
    }
    RRect rRect = {};
    if (StrokeLineToRRect(paint->stroke, line, &rRect)) {
      LayerPaint fillPaint = *paint;
      fillPaint.style = PaintStyle::Fill;
      addRRect(rRect, &fillPaint, matrix);
      return true;
    }
    return false;
  }
  Rect rect = {};
  bool closed = false;
  // isRect can match an open 4-segment polyline whose stroke (open contour with caps) differs
  // from a closed rectangle stroke, so only forward when the path is closed or there is no
  // stroke (closedness does not affect fill).
  if (path.isRect(&rect, &closed) &&
      (paint == nullptr || paint->style != PaintStyle::Stroke || closed)) {
    addRect(rect, paint, matrix);
    return true;
  }
  RRect rRect = {};
  // isRRect returns false for oval/circle paths, so check isOval explicitly.
  if (path.isOval(&rect)) {
    rRect.setOval(rect);
    addRRect(rRect, paint, matrix);
    return true;
  }
  if (path.isRRect(&rRect)) {
    addRRect(rRect, paint, matrix);
    return true;
  }
  return false;
}

bool LayerRecorder::canAppend(PendingType type, const LayerPaint* paint,
                              const Matrix& matrix) const {
  if (pendingType != type) {
    return false;
  }
  // A batch shares one paint, so one no paint captured can never join a painted batch: the batch
  // would have to hold a paint that was never given.
  if (pendingPaint.has_value() != (paint != nullptr)) {
    return false;
  }
  if (pendingMatrix != matrix) {
    return false;
  }
  if (pendingPaint.has_value()) {
    if (pendingPaint->placement != paint->placement || pendingPaint->style != paint->style ||
        pendingPaint->blendMode != paint->blendMode) {
      return false;
    }
    if (pendingPaint->color != paint->color) {
      return false;
    }
    if (pendingPaint->shader != paint->shader) {
      return false;
    }
    if (pendingPaint->style == PaintStyle::Stroke) {
      if (pendingPaint->stroke.width != paint->stroke.width ||
          pendingPaint->stroke.cap != paint->stroke.cap ||
          pendingPaint->stroke.join != paint->stroke.join ||
          pendingPaint->stroke.miterLimit != paint->stroke.miterLimit) {
        return false;
      }
    }
  }
  return true;
}

void LayerRecorder::flushPending(PendingType newType, const LayerPaint* newPaint,
                                 const Matrix& newMatrix) {
  if (pendingType != PendingType::None) {
    std::unique_ptr<GeometryContent> content = nullptr;
    const LayerPaint* pending = pendingPaint.has_value() ? &*pendingPaint : nullptr;
    auto& list = (pending != nullptr ? pending->placement : LayerPlacement::Background) ==
                         LayerPlacement::Foreground
                     ? foregrounds
                     : contents;
    switch (pendingType) {
      case PendingType::Rect:
        if (pendingRects.size() == 1) {
          content = std::make_unique<RectContent>(pendingRects[0], pending);
        } else {
          content = std::make_unique<RectsContent>(std::move(pendingRects), pending);
        }
        pendingRects = {};
        break;
      case PendingType::RRect:
        if (pendingRRects.size() == 1) {
          content = std::make_unique<RRectContent>(pendingRRects[0], pending);
        } else {
          content = std::make_unique<RRectsContent>(std::move(pendingRRects), pending);
        }
        pendingRRects = {};
        break;
      case PendingType::Shape:
        // Route to ShapeContent whenever the paint carries a complex stroke style, even if the
        // shape is a simple path. PathContent otherwise ignores strokeAlign and pathEffect.
        if (pendingShape->isSimplePath() && !ComplexStrokeStyle(pending)) {
          content = std::make_unique<PathContent>(pendingShape->getPath(), pending);
        } else {
          content = std::make_unique<ShapeContent>(std::move(pendingShape), pending);
        }
        pendingShape = nullptr;
        break;
      default:
        break;
    }

    // Wrap with MatrixContent if matrix is not identity.
    if (content != nullptr && !pendingMatrix.isIdentity()) {
      content = std::make_unique<MatrixContent>(std::move(content), pendingMatrix);
    }
    DEBUG_ASSERT(content != nullptr);
    if (content != nullptr) {
      list.push_back(std::move(content));
    }
  }
  pendingType = newType;
  pendingPaint = newPaint != nullptr ? std::optional<LayerPaint>(*newPaint) : std::nullopt;
  pendingMatrix = newMatrix;
}

// Entries sharing a geometry need one representative draw as they all cover the same outline.
// Image shaders are the exception: each covers a different part, so all of them are kept. A bare
// entry stands for the geometry only when the query includes it, which keeps it out of the contour.
static std::vector<GeometryContent*> SelectContourRepresentatives(
    const std::vector<GeometryContent*>& group, bool includeUnpaintedGeometry) {
  std::vector<GeometryContent*> candidates = {};
  candidates.reserve(group.size());
  for (const auto content : group) {
    if (content->hasPaint() || includeUnpaintedGeometry) {
      candidates.push_back(content);
    }
  }
  auto nonImageContent = std::find_if(candidates.begin(), candidates.end(), [](const auto content) {
    return content->getShader() == nullptr || !content->getShader()->isAImage();
  });
  if (nonImageContent == candidates.end()) {
    return candidates;
  }
  return {*nonImageContent};
}

std::unique_ptr<LayerContent> LayerRecorder::finishRecording() {
  flushPending();

  if (contents.empty() && foregrounds.empty()) {
    return nullptr;
  }

  if (contents.size() == 1 && foregrounds.empty()) {
    return std::move(contents[0]);
  }

  // Merge contents and foregrounds.
  auto foregroundStartIndex = contents.size();
  std::vector<std::unique_ptr<GeometryContent>> allContents = {};
  allContents.reserve(contents.size() + foregrounds.size());
  for (auto& content : contents) {
    allContents.push_back(std::move(content));
  }
  for (auto& content : foregrounds) {
    allContents.push_back(std::move(content));
  }

  // Phase 1: Group by geometry.
  std::vector<std::vector<GeometryContent*>> groups = {};
  for (const auto& content : allContents) {
    if (groups.empty() || !groups.back()[0]->hasSameGeometry(content.get())) {
      groups.push_back({content.get()});
    } else {
      groups.back().push_back(content.get());
    }
  }

  // Phase 2: one contour selection per group and caliber. A group is painted or bare as a whole
  // (hasSameGeometry() keeps a painted entry away from a bare one), so the painted caliber selects
  // nothing for a bare group. The table is built only when a group collapses several entries into
  // one representative; the painted table is materialized only when the two calibers actually
  // select differently.
  bool buildTable = false;
  std::vector<std::vector<GeometryContent*>> selections = {};
  std::vector<std::vector<GeometryContent*>> paintedSelections = {};
  selections.reserve(groups.size());
  paintedSelections.reserve(groups.size());
  for (const auto& group : groups) {
    selections.push_back(SelectContourRepresentatives(group, true));
    paintedSelections.push_back(SelectContourRepresentatives(group, false));
    if (group.size() > 1 && selections.back().size() == 1) {
      buildTable = true;
    }
  }
  std::vector<GeometryContent*> contours = {};
  std::optional<std::vector<GeometryContent*>> paintedContours = std::nullopt;
  if (buildTable) {
    bool paintedDiffers = false;
    for (size_t i = 0; i < groups.size(); i++) {
      contours.insert(contours.end(), selections[i].begin(), selections[i].end());
      // Compared by content rather than by size: two selections of the same size may still hold
      // different entries, and the painted caliber must not reuse the table in that case.
      if (selections[i] != paintedSelections[i]) {
        paintedDiffers = true;
      }
    }
    if (paintedDiffers) {
      std::vector<GeometryContent*> painted = {};
      for (size_t i = 0; i < groups.size(); i++) {
        painted.insert(painted.end(), paintedSelections[i].begin(), paintedSelections[i].end());
      }
      paintedContours = std::move(painted);
    }
  }
  return std::make_unique<ComposeContent>(std::move(allContents), foregroundStartIndex,
                                          std::move(contours), std::move(paintedContours));
}

}  // namespace tgfx
