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
 * LayerRecorder records geometries and their paints as layer content.  Geometries with invisible
 * paints are still included as part of the layer's contour, but they will not be rendered.
 */
class LayerRecorder {
 public:
  LayerRecorder();
  ~LayerRecorder();

  /**
   * Adds a rectangle with the specified paint and an optional transformation matrix.
   * @param rect The rectangle shape.
   * @param paint The paint style for the rectangle.
   * @param matrix Optional transformation matrix. If nullopt, no transformation is applied.
   */
  void addRect(const Rect& rect, const LayerPaint& paint,
               const std::optional<Matrix>& matrix = std::nullopt);

  /**
   * Adds a rounded rectangle with the specified paint and an optional transformation matrix.
   * @param rRect The rounded rectangle shape.
   * @param paint The paint style for the rounded rectangle.
   * @param matrix Optional transformation matrix. If nullopt, no transformation is applied.
   */
  void addRRect(const RRect& rRect, const LayerPaint& paint,
                const std::optional<Matrix>& matrix = std::nullopt);

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
   * Adds a text blob with the specified paint and transformation matrix.
   * @param textBlob The text blob.
   * @param paint The paint style for the text.
   * @param matrix The transformation matrix to apply to the text.
   */
  void addTextBlob(std::shared_ptr<TextBlob> textBlob, const LayerPaint& paint,
                   const Matrix& matrix);

 private:
  enum class PendingType {
    None,
    Rect,
    RRect,
    Shape,
  };

  std::vector<std::unique_ptr<GeometryContent>> contents;
  std::vector<std::unique_ptr<GeometryContent>> foregrounds;

  PendingType pendingType = PendingType::None;
  LayerPaint pendingPaint = {};
  std::optional<Matrix> pendingMatrix = std::nullopt;
  std::vector<Rect> pendingRects = {};
  std::vector<RRect> pendingRRects = {};
  std::shared_ptr<Shape> pendingShape = nullptr;

  bool handlePathAsRect(const Path& path, const LayerPaint& paint,
                        const std::optional<Matrix>& matrix = std::nullopt);
  bool canAppend(PendingType type, const LayerPaint& paint,
                 const std::optional<Matrix>& matrix) const;
  void flushPending(PendingType newType = PendingType::None, const LayerPaint& newPaint = {},
                    const std::optional<Matrix>& newMatrix = std::nullopt);

  std::unique_ptr<LayerContent> finishRecording();

  friend class Layer;
};
}  // namespace tgfx
