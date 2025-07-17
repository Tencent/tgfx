/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/PathEffect.h"
#include "tgfx/core/PathProvider.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
class UniqueKey;

/**
 * Shape represents a deferred Path object. It postpones path computations, such as PathOps and
 * PathEffects, until the path is actually required. Using Shape is recommended when the path is
 * expensive to compute and not required immediately. It can leverage multi-threading to compute the
 * path in parallel during drawing and cache the rasterized form in the GPU for repeated drawing.
 * Shape is thread-safe and immutable once created.
 */
class Shape {
 public:
  /**
   * Wraps an existing path in a Shape object. Returns nullptr if the path is empty and not an
   * inverse fill type.
   */
  static std::shared_ptr<Shape> MakeFrom(Path path);

  /**
   * Creates a new Shape from the given text blob. The specified scale is applied to the text blob
   * before extracting its paths. Returns nullptr if the text blob is nullptr or if none of the
   * glyphs in the blob can generate a path, such as when using bitmap typefaces.
   */
  static std::shared_ptr<Shape> MakeFrom(std::shared_ptr<TextBlob> textBlob, float scale = 1.0f);

  /**
   * Creates a new Shape from the given PathProvider. Returns nullptr if pathProvider is nullptr.
   */
  static std::shared_ptr<Shape> MakeFrom(std::shared_ptr<PathProvider> pathProvider);

  /**
   * Creates a new Shape from the Font and glyphID. Returns nullptr if the glyphID is 0
   * or contains a typeface that can't generate a path, such as bitmap typefaces.
   */
  static std::shared_ptr<Shape> MakeFrom(Font font, GlyphID glyphID);

  /**
   * Merges two Shapes into a new Shape using the specified path operation. If either Shape is
   * nullptr, the other Shape is returned. Returns nullptr if both Shapes are nullptr.
   */
  static std::shared_ptr<Shape> Merge(std::shared_ptr<Shape> first, std::shared_ptr<Shape> second,
                                      PathOp pathOp = PathOp::Append);

  /**
   * Merges multiple Shapes into a new Shape using the PathOp::Append operation. Returns nullptr if
   * the shape vector is empty.
   */
  static std::shared_ptr<Shape> Merge(const std::vector<std::shared_ptr<Shape>>& shapes);

  /**
   * Applies the specified stroke to the given Shape. If the stroke is nullptr, the original Shape
   * is returned. Returns nullptr if the Shape is nullptr.
   */
  static std::shared_ptr<Shape> ApplyStroke(std::shared_ptr<Shape> shape, const Stroke* stroke);

  /**
   * Applies the specified matrix to the given Shape. If the matrix is identity, the original Shape
   * is returned. Returns nullptr if the Shape is nullptr.
   */
  static std::shared_ptr<Shape> ApplyMatrix(std::shared_ptr<Shape> shape, const Matrix& matrix);

  /**
   * Applies the specified path effect to the given Shape. If the effect is nullptr, the original
   * Shape is returned. Returns nullptr if the Shape is nullptr.
   */
  static std::shared_ptr<Shape> ApplyEffect(std::shared_ptr<Shape> shape,
                                            std::shared_ptr<PathEffect> effect);

  /**
   * Creates a new Shape by applying the inverse fill type to the given Shape. Returns nullptr if
   * the shape is nullptr.
   */
  static std::shared_ptr<Shape> ApplyInverse(std::shared_ptr<Shape> shape);

  virtual ~Shape() = default;

  /**
   * Returns true if the Shape contains a simple path that can be directly retrieved using getPath()
   * without extra computation.
   */
  virtual bool isSimplePath() const {
    return false;
  }

  /**
   * Returns true if the PathFillType of the computed path is InverseWinding or InverseEvenOdd.
   */
  virtual bool isInverseFillType() const {
    return false;
  }

  /**
   * Returns the bounding box of the Shape. The bounds might be larger than the actual shape because
   * the exact bounds can't be determined until the shape is computed.
   */
  virtual Rect getBounds() const = 0;

  /**
   * Returns the Shape's computed path.  Note: The path is recalculated each time this method is
   * called, as it is not cached.
   */
  virtual Path getPath() const = 0;

 protected:
  enum class Type { Append, Effect, Text, Inverse, Matrix, Merge, Path, Stroke, Provider, Glyph };

  /**
   * Returns the type of the Shape.
   */
  virtual Type type() const = 0;

  /**
   * Generates a unique key for the Shape. The key is used to cache the rasterized form of the Shape
   * in the GPU.
   */
  virtual UniqueKey getUniqueKey() const = 0;

  friend class AppendShape;
  friend class StrokeShape;
  friend class MatrixShape;
  friend class ShapeDrawOp;
  friend class ProxyProvider;
  friend class Canvas;
  friend class Types;
};
}  // namespace tgfx
