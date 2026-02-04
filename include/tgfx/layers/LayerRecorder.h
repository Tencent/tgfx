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
class LayerContent;
class GeometryContent;

/**
 * LayerRecorder records geometries and their paints as layer content. Geometries with invisible
 * paints are still included as part of the layer's contour, but they will not be rendered.
 */
class LayerRecorder {
 public:
  LayerRecorder();
  ~LayerRecorder();

  /**
   * Adds a rectangle with the specified paint.
   * @param rect The rectangle shape.
   * @param paint The paint style for the rectangle.
   */
  void addRect(const Rect& rect, const LayerPaint& paint);

  /**
   * Adds a rounded rectangle with the specified paint.
   * @param rRect The rounded rectangle shape.
   * @param paint The paint style for the rounded rectangle.
   */
  void addRRect(const RRect& rRect, const LayerPaint& paint);

  /**
   * Adds a path with the specified paint.
   * @param path The path shape.
   * @param paint The paint style for the path.
   */
  void addPath(const Path& path, const LayerPaint& paint);

  /**
   * Adds a shape with the specified paint.
   * @param shape The shape object.
   * @param paint The paint style for the shape.
   */
  void addShape(std::shared_ptr<Shape> shape, const LayerPaint& paint);

  /**
   * Adds a text blob with the specified paint.
   * @param textBlob The text blob.
   * @param paint The paint style for the text.
   * @param x The x offset of the text blob.
   * @param y The y offset of the text blob.
   */
  void addTextBlob(std::shared_ptr<TextBlob> textBlob, const LayerPaint& paint, float x = 0,
                   float y = 0);

  /**
   * Returns the current transformation matrix. If no matrix is set, returns the identity matrix.
   */
  const Matrix& getMatrix() const;

  /**
   * Sets the transformation matrix for subsequent drawing operations.
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
  std::optional<Matrix> _matrix = std::nullopt;

  std::vector<std::unique_ptr<GeometryContent>> contents;
  std::vector<std::unique_ptr<GeometryContent>> foregrounds;

  PendingType pendingType = PendingType::None;
  LayerPaint pendingPaint = {};
  // Transformation matrix for the pending geometries.
  std::optional<Matrix> pendingMatrix = std::nullopt;
  std::vector<Rect> pendingRects = {};
  std::vector<RRect> pendingRRects = {};
  std::shared_ptr<Shape> pendingShape = nullptr;

  void addRect(const Rect& rect, const LayerPaint& paint, const std::optional<Matrix>& matrix);
  void addRRect(const RRect& rRect, const LayerPaint& paint, const std::optional<Matrix>& matrix);
  void addPath(const Path& path, const LayerPaint& paint, const std::optional<Matrix>& matrix);
  void addShape(std::shared_ptr<Shape> shape, const LayerPaint& paint,
                const std::optional<Matrix>& matrix);

  bool handlePathAsRect(const Path& path, const LayerPaint& paint,
                        const std::optional<Matrix>& matrix);
  bool canAppend(PendingType type, const LayerPaint& paint,
                 const std::optional<Matrix>& matrix) const;
  void flushPending(PendingType newType = PendingType::None, const LayerPaint& newPaint = {},
                    const std::optional<Matrix>& newMatrix = std::nullopt);

  std::unique_ptr<LayerContent> finishRecording();

  friend class Layer;
};
}  // namespace tgfx
