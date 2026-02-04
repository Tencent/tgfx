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
#include "layers/contents/PathContent.h"
#include "layers/contents/RRectContent.h"
#include "layers/contents/RRectsContent.h"
#include "layers/contents/RectContent.h"
#include "layers/contents/RectsContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/TextContent.h"

namespace tgfx {

LayerRecorder::LayerRecorder() = default;

LayerRecorder::~LayerRecorder() = default;

void LayerRecorder::addRect(const Rect& rect, const LayerPaint& paint) {
  addRect(rect, paint, _matrix);
}

void LayerRecorder::addRRect(const RRect& rRect, const LayerPaint& paint) {
  addRRect(rRect, paint, _matrix);
}

void LayerRecorder::addPath(const Path& path, const LayerPaint& paint) {
  addPath(path, paint, _matrix);
}

void LayerRecorder::addShape(std::shared_ptr<Shape> shape, const LayerPaint& paint) {
  addShape(std::move(shape), paint, _matrix);
}

void LayerRecorder::addTextBlob(std::shared_ptr<TextBlob> textBlob, const LayerPaint& paint,
                                float x, float y) {
  if (textBlob == nullptr) {
    return;
  }
  flushPending();
  auto& list = paint.placement == LayerPlacement::Foreground ? foregrounds : contents;
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

const Matrix& LayerRecorder::getMatrix() const {
  return _matrix;
}

void LayerRecorder::setMatrix(const Matrix& matrix) {
  _matrix = matrix;
}

void LayerRecorder::resetMatrix() {
  _matrix = Matrix::I();
}

void LayerRecorder::addRect(const Rect& rect, const LayerPaint& paint, const Matrix& matrix) {
  if (rect.isEmpty()) {
    return;
  }
  if (!canAppend(PendingType::Rect, paint, matrix)) {
    flushPending(PendingType::Rect, paint, matrix);
  }
  pendingRects.push_back(rect);
}

void LayerRecorder::addRRect(const RRect& rRect, const LayerPaint& paint, const Matrix& matrix) {
  if (rRect.rect.isEmpty()) {
    return;
  }
  if (rRect.isRect()) {
    addRect(rRect.rect, paint, matrix);
    return;
  }
  if (!canAppend(PendingType::RRect, paint, matrix)) {
    flushPending(PendingType::RRect, paint, matrix);
  }
  pendingRRects.push_back(rRect);
}

void LayerRecorder::addPath(const Path& path, const LayerPaint& paint, const Matrix& matrix) {
  if (path.isEmpty()) {
    return;
  }
  if (handlePathAsRect(path, paint, matrix)) {
    return;
  }
  flushPending(PendingType::Shape, paint, matrix);
  pendingShape = Shape::MakeFrom(path);
}

void LayerRecorder::addShape(std::shared_ptr<Shape> shape, const LayerPaint& paint,
                             const Matrix& matrix) {
  if (shape == nullptr) {
    return;
  }
  if (shape->isSimplePath()) {
    addPath(shape->getPath(), paint, matrix);
    return;
  }
  do {
    auto matrixShape = ShapeUtils::AsMatrixShape(shape.get());
    if (matrixShape == nullptr || !matrixShape->shape->isSimplePath()) {
      break;
    }
    // Skip handlePathAsRect for stroke with non-uniform scale in shape's matrix, as this
    // optimization would cause the stroke to be scaled non-uniformly.
    auto scales = matrixShape->matrix.getAxisScales();
    if (paint.style == PaintStyle::Stroke && !FloatNearlyEqual(scales.x, scales.y)) {
      break;
    }
    auto combinedMatrix = matrixShape->matrix;
    combinedMatrix.postConcat(matrix);
    if (handlePathAsRect(matrixShape->shape->getPath(), paint, combinedMatrix)) {
      return;
    }
  } while (false);
  flushPending(PendingType::Shape, paint, matrix);
  pendingShape = std::move(shape);
}

bool LayerRecorder::handlePathAsRect(const Path& path, const LayerPaint& paint,
                                     const Matrix& matrix) {
  Point line[2] = {};
  if (path.isLine(line)) {
    if (paint.style != PaintStyle::Stroke) {
      // A line cannot be filled.
      return true;
    }
    Rect rect = {};
    if (StrokeLineToRect(paint.stroke, line, &rect)) {
      LayerPaint fillPaint = paint;
      fillPaint.style = PaintStyle::Fill;
      addRect(rect, fillPaint, matrix);
      return true;
    }
  }
  if (Rect rect = {}; path.isRect(&rect)) {
    addRect(rect, paint, matrix);
    return true;
  }
  if (RRect rRect = {}; path.isRRect(&rRect)) {
    addRRect(rRect, paint, matrix);
    return true;
  }
  return false;
}

bool LayerRecorder::canAppend(PendingType type, const LayerPaint& paint,
                              const Matrix& matrix) const {
  if (pendingType != type) {
    return false;
  }
  if (pendingMatrix != matrix) {
    return false;
  }
  if (pendingPaint.placement != paint.placement || pendingPaint.style != paint.style ||
      pendingPaint.blendMode != paint.blendMode) {
    return false;
  }
  if (pendingPaint.color != paint.color) {
    return false;
  }
  if (pendingPaint.shader != paint.shader) {
    return false;
  }
  if (pendingPaint.style == PaintStyle::Stroke) {
    if (pendingPaint.stroke.width != paint.stroke.width ||
        pendingPaint.stroke.cap != paint.stroke.cap ||
        pendingPaint.stroke.join != paint.stroke.join ||
        pendingPaint.stroke.miterLimit != paint.stroke.miterLimit) {
      return false;
    }
  }
  return true;
}

void LayerRecorder::flushPending(PendingType newType, const LayerPaint& newPaint,
                                 const Matrix& newMatrix) {
  if (pendingType != PendingType::None) {
    std::unique_ptr<GeometryContent> content = nullptr;
    auto& list = pendingPaint.placement == LayerPlacement::Foreground ? foregrounds : contents;
    switch (pendingType) {
      case PendingType::Rect:
        if (pendingRects.size() == 1) {
          content = std::make_unique<RectContent>(pendingRects[0], pendingPaint);
        } else {
          content = std::make_unique<RectsContent>(std::move(pendingRects), pendingPaint);
        }
        pendingRects = {};
        break;
      case PendingType::RRect:
        if (pendingRRects.size() == 1) {
          content = std::make_unique<RRectContent>(pendingRRects[0], pendingPaint);
        } else {
          content = std::make_unique<RRectsContent>(std::move(pendingRRects), pendingPaint);
        }
        pendingRRects = {};
        break;
      case PendingType::Shape:
        if (pendingShape->isSimplePath()) {
          content = std::make_unique<PathContent>(pendingShape->getPath(), pendingPaint);
        } else {
          content = std::make_unique<ShapeContent>(std::move(pendingShape), pendingPaint);
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
  pendingPaint = newPaint;
  pendingMatrix = newMatrix;
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

  // Phase 2: Collect contours from groups.
  std::vector<GeometryContent*> contours = {};
  bool needContours = false;
  for (const auto& group : groups) {
    auto nonImageContent = std::find_if(group.begin(), group.end(), [](auto content) {
      return content->getShader() == nullptr || !content->getShader()->isAImage();
    });
    if (nonImageContent == group.end()) {
      // All image shaders, collect all.
      for (auto content : group) {
        contours.push_back(content);
      }
    } else {
      contours.push_back(*nonImageContent);
      if (group.size() > 1) {
        needContours = true;
      }
    }
  }
  if (!needContours) {
    contours.clear();
  }

  return std::make_unique<ComposeContent>(std::move(allContents), foregroundStartIndex,
                                          std::move(contours));
}

}  // namespace tgfx
