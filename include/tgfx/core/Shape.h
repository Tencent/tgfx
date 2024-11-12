/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "tgfx/core/RRect.h"
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
   * Wraps an existing path in a Shape object. Returns nullptr if the path is empty.
   */
  static std::shared_ptr<Shape> MakeFrom(Path path);

  /**
   * Creates a new Shape from the given text blob. Returns nullptr if the text blob is nullptr or
   * contains a typeface that can't generate a path, such as color emoji typefaces.
   */
  static std::shared_ptr<Shape> MakeFrom(std::shared_ptr<TextBlob> textBlob);

  /**
   * Merges two Shapes into a new Shape using the specified path operation. If either Shape is
   * nullptr, the other Shape is returned. Returns nullptr if both Shapes are nullptr.
   */
  static std::shared_ptr<Shape> Merge(std::shared_ptr<Shape> first, std::shared_ptr<Shape> second,
                                      PathOp pathOp = PathOp::Append);

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

  virtual ~Shape() = default;

  /**
   * Returns true if the Shape is a Line and stores points in line. Otherwise, returns false and
   * leaves line unchanged.
   */
  virtual bool isLine(Point line[2] = nullptr) const;

  /**
   * Returns true if the Shape is a Rect and stores the Rect in rect. Otherwise, returns false and
   * leaves rect unchanged.
   */
  virtual bool isRect(Rect* rect = nullptr) const;

  /**
   * Returns true if the Shape is an oval or circle and stores the bounding Rect in bounds.
   * Otherwise, returns false and leaves the bounds unchanged.
   */
  virtual bool isOval(Rect* bounds = nullptr) const;

  /**
   * Returns true if the Shape is a RRect and stores the RRect in rRect. Otherwise, returns false
   * and leaves rRect unchanged. Please note that this method returns false if the path is
   * representable as oval, circle, or Rect.
   */
  virtual bool isRRect(RRect* rRect = nullptr) const;

  /**
   * Returns the bounding box of the Shape. The bounds might be larger than the actual shape because
   * the exact bounds can't be determined until the shape is computed. Note: Since the Shape may
   * contain strokes or text glyphs whose outlines can change with different scale factors, it's
   * best to pass the final drawing scale factor in the resolutionScale for computing the bounds to
   * ensure accuracy. However, the resolutionScale is not applied to the returned bounds; it just
   * affects the precision of the bounds.
   * @param resolutionScale The intended resolution for the Shape. The default value is 1.0. Higher
   * values (res > 1) mean the result should be more precise, as it will be zoomed up and small
   * errors will be magnified. Lower values (0 < res < 1) mean the result can be less precise, as it
   * will be zoomed down and small errors may be invisible.
   * @return The bounding box of the Shape.
   */
  virtual Rect getBounds(float resolutionScale = 1.0f) const;

  /**
   * Returns the computed path of the Shape. Note: Since the Shape may contain strokes or text
   * glyphs whose outlines can change with different scale factors, it's best to pass the final
   * drawing scale factor in the resolutionScale for computing the path to ensure accuracy. However,
   * the resolutionScale is not applied to the returned path; it just affects the precision of the
   * path.
   * @param resolutionScale The intended resolution for the Shape. The default value is 1.0. Higher
   * values (res > 1) mean the result should be more precise, as it will be zoomed up and small
   * errors will be magnified. Lower values (0 < res < 1) mean the result can be less precise, as it
   * will be zoomed down and small errors may be invisible.
   * @return The computed path of the Shape.
   */
  virtual Path getPath(float resolutionScale = 1.0f) const;

 protected:
  enum class Type { Append, Effect, Glyph, Matrix, Merge, Path, Stroke };

  /**
   * Returns the type of the Shape.
   */
  virtual Type type() const = 0;

  /**
   * Generates a unique key for the Shape. The key is used to cache the rasterized form of the Shape
   * in the GPU.
   */
  virtual UniqueKey getUniqueKey() const = 0;

 private:
  static void Append(std::vector<std::shared_ptr<Shape>>* shapes, std::shared_ptr<Shape> shape);

  friend class StrokeShape;
  friend class MatrixShape;
  friend class ShapeDrawOp;
  friend class ProxyProvider;
  friend class Canvas;
};
}  // namespace tgfx
