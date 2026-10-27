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

#pragma once

#include <memory>
#include <optional>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/LayerPaint.h"

namespace tgfx {
class Mesh;
class LayerContent;
class GeometryContent;

/**
 * LayerRecorder records geometries and their paints as layer content. A geometry may also be
 * recorded without a paint: it is never drawn, contributes no shared contour, and only a style
 * declaring LayerStyleContentSource::Geometry covers it.
 */
class LayerRecorder {
 public:
  LayerRecorder();
  ~LayerRecorder();

  /**
   * Adds a rectangle with the specified paint.
   * @param rect The rectangle shape.
   * @param paint The paint for this geometry. Pass nullptr to add the geometry without a paint:
   *              it is never drawn, contributes no shared contour, and only a style declaring
   *              LayerStyleContentSource::Geometry covers it.
   */
  void addRect(const Rect& rect, const LayerPaint* paint);

  /**
   * Adds a rounded rectangle with the specified paint.
   * @param rRect The rounded rectangle shape.
   * @param paint Same as addRect(): nullptr adds the geometry without a paint.
   */
  void addRRect(const RRect& rRect, const LayerPaint* paint);

  /**
   * Adds a path with the specified paint.
   * @param path The path shape.
   * @param paint Same as addRect(): nullptr adds the geometry without a paint.
   */
  void addPath(const Path& path, const LayerPaint* paint);

  /**
   * Adds a shape with the specified paint.
   * @param shape The shape object.
   * @param paint Same as addRect(): nullptr adds the geometry without a paint.
   */
  void addShape(std::shared_ptr<Shape> shape, const LayerPaint* paint);

  /**
   * Adds a text blob with the specified paint.
   * @param textBlob The text blob.
   * @param paint Same as addRect(): nullptr adds the geometry without a paint.
   * @param x The x offset of the text blob.
   * @param y The y offset of the text blob.
   */
  void addTextBlob(std::shared_ptr<TextBlob> textBlob, const LayerPaint* paint, float x = 0,
                   float y = 0);

  /**
   * Adds a mesh with the specified paint.
   * @param mesh The mesh object.
   * @param paint Same as addRect(): nullptr adds the geometry without a paint.
   */
  void addMesh(std::shared_ptr<Mesh> mesh, const LayerPaint* paint);

  /**
   * Returns the current transformation matrix. If no matrix is set, returns the identity matrix.
   */
  const Matrix& getMatrix() const;

  /**
   * Sets the transformation matrix for subsequent drawing operations. The matrix is applied to both
   * the geometry (Rect, RRect, Path, Shape, Mesh, TextBlob) and the paint.
   * @param matrix The transformation matrix to apply.
   */
  void setMatrix(const Matrix& matrix);

  /**
   * Resets the transformation matrix to none.
   */
  void resetMatrix();

 private:
  enum class PendingType {
    None,
    Rect,
    RRect,
    Shape,
  };

  // Current transformation matrix applied to all subsequent drawing operations.
  Matrix _matrix = Matrix::I();

  std::vector<std::unique_ptr<GeometryContent>> contents;
  std::vector<std::unique_ptr<GeometryContent>> foregrounds;

  PendingType pendingType = PendingType::None;
  // The paint the pending geometries share; nullopt when no paint captured them. A batch only ever
  // holds entries of one kind.
  std::optional<LayerPaint> pendingPaint = std::nullopt;
  // Transformation matrix for the pending geometries.
  Matrix pendingMatrix = Matrix::I();
  std::vector<Rect> pendingRects = {};
  std::vector<RRect> pendingRRects = {};
  std::shared_ptr<Shape> pendingShape = nullptr;

  void addRect(const Rect& rect, const LayerPaint* paint, const Matrix& matrix);
  void addRRect(const RRect& rRect, const LayerPaint* paint, const Matrix& matrix);
  void addPath(const Path& path, const LayerPaint* paint, const Matrix& matrix);
  void addShape(std::shared_ptr<Shape> shape, const LayerPaint* paint, const Matrix& matrix);

  bool tryAddSimplifiedPath(const Path& path, const LayerPaint* paint, const Matrix& matrix);
  bool tryAddSimplifiedMatrixShape(std::shared_ptr<Shape> shape, const LayerPaint* paint,
                                   const Matrix& matrix);
  bool canAppend(PendingType type, const LayerPaint* paint, const Matrix& matrix) const;
  void flushPending(PendingType newType = PendingType::None, const LayerPaint* newPaint = nullptr,
                    const Matrix& newMatrix = Matrix::I());

  std::unique_ptr<LayerContent> finishRecording();

  friend class Layer;
};
}  // namespace tgfx
