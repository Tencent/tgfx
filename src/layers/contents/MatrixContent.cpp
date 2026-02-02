/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "MatrixContent.h"
#include "core/utils/Types.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

MatrixContent::MatrixContent(std::unique_ptr<GeometryContent> content, const Matrix& matrix)
    : content(std::move(content)), _matrix(matrix) {
}

Rect MatrixContent::getBounds() const {
  return _matrix.mapRect(content->getBounds());
}

bool MatrixContent::hasSameGeometry(const GeometryContent* other) const {
  if (other == nullptr || Types::Get(this) != Types::Get(other)) {
    return false;
  }
  auto otherMatrix = static_cast<const MatrixContent*>(other);
  return _matrix == otherMatrix->_matrix &&
         content->hasSameGeometry(otherMatrix->content.get());
}

const Color& MatrixContent::getColor() const {
  return content->getColor();
}

const std::shared_ptr<Shader>& MatrixContent::getShader() const {
  return content->getShader();
}

const BlendMode& MatrixContent::getBlendMode() const {
  return content->getBlendMode();
}

Rect MatrixContent::getTightBounds(const Matrix& matrix, const Stroke* stroke) const {
  auto combinedMatrix = _matrix;
  combinedMatrix.postConcat(matrix);
  return content->getTightBounds(combinedMatrix, stroke);
}

bool MatrixContent::hitTestPoint(float localX, float localY, const Stroke* stroke) const {
  Matrix inverse = Matrix::I();
  if (!_matrix.invert(&inverse)) {
    return false;
  }
  auto localPoint = inverse.mapXY(localX, localY);
  return content->hitTestPoint(localPoint.x, localPoint.y, stroke);
}

bool MatrixContent::drawDefault(Canvas* canvas, float alpha, bool antiAlias,
                                const Stroke* stroke) const {
  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(_matrix);
  return content->drawDefault(canvas, alpha, antiAlias, stroke);
}

void MatrixContent::drawForeground(Canvas* canvas, float alpha, bool antiAlias,
                                   const Stroke* stroke) const {
  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(_matrix);
  content->drawForeground(canvas, alpha, antiAlias, stroke);
}

void MatrixContent::drawContour(Canvas* canvas, bool antiAlias, const Stroke* stroke) const {
  AutoCanvasRestore autoRestore(canvas);
  canvas->concat(_matrix);
  content->drawContour(canvas, antiAlias, stroke);
}

bool MatrixContent::contourEqualsOpaqueContent() const {
  return content->contourEqualsOpaqueContent();
}

}  // namespace tgfx
