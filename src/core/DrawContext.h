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

#pragma once

#include <stack>
#include "core/FillStyle.h"
#include "core/GlyphRun.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"

namespace tgfx {
struct MCState {
  Matrix matrix = Matrix::I();
  Path clip = {};
};

/**
 * DrawContext is an abstract class that provides a drawing API for rendering graphics primitives.
 */
class DrawContext {
 public:
  DrawContext();

  virtual ~DrawContext() = default;

  /**
   * Returns the current total matrix.
   */
  virtual const Matrix& getMatrix() const {
    return state.matrix;
  }

  /**
   * Returns the current total clip Path.
   */
  virtual const Path& getClip() const {
    return state.clip;
  }

  /**
   * Saves matrix and clip. Calling restore() discards changes to them, restoring them to their
   * state when save() was called. Saved Canvas state is put on a stack, multiple calls to save()
   * should be balanced by an equal number of calls to restore().
   */
  virtual void save();

  /**
   * Removes changes to matrix and clip since Canvas state was last saved. The state is removed from
   * the stack. Does nothing if the stack is empty.
   */
  virtual void restore();

  /**
   * Translates the current matrix by dx along the x-axis and dy along the y-axis. Mathematically,
   * it replaces the current matrix with a translation matrix premultiplied with the current matrix.
   * This has the effect of moving the drawing by (dx, dy) before transforming the result with the
   * current matrix.
   */
  virtual void translate(float dx, float dy);

  /**
   * Scales the current matrix by sx along the x-axis and sy along the y-axis. Mathematically, it
   * replaces the current matrix with a scale matrix premultiplied with the current matrix. This has
   * the effect of scaling the drawing by (sx, sy) before transforming the result with the current
   * matrix.
   */
  virtual void scale(float sx, float sy);

  /**
   * Rotates the current matrix by degrees. Positive values rotate the drawing clockwise.
   * Mathematically, it replaces the current matrix with a rotation matrix premultiplied with the
   * current matrix. This has the effect of rotating the drawing by degrees before transforming the
   * result with the current matrix.
   */
  virtual void rotate(float degrees);

  /**
   * Rotates the current matrix by degrees around the point (px, py). Positive values rotate the
   * drawing clockwise. Mathematically, it replaces the current matrix with a rotation matrix
   * premultiplied with the current matrix. This has the effect of rotating the drawing around the
   * point (px, py) by degrees before transforming the result with the current matrix.
   */
  virtual void rotate(float degress, float px, float py);

  /**
   * Skews the current matrix by sx along the x-axis and sy along the y-axis. A positive value of sx
   * skews the drawing right as y-axis values increase; a positive value of sy skews the drawing
   * down as x-axis values increase. Mathematically, it replaces the current matrix with a skew
   * matrix premultiplied with the current matrix. This has the effect of skewing the drawing by
   * (sx, sy) before transforming the result with the current matrix.
   */
  virtual void skew(float sx, float sy);

  /**
   * Replaces the current Matrix with matrix premultiplied with the existing one. This has the
   * effect of transforming the drawn geometry by matrix, before transforming the result with the
   * existing Matrix.
   */
  virtual void concat(const Matrix& matrix);

  /**
   * Replaces the current Matrix to the provided matrix, discarding any previous matrix state. The
   * new matrix is post-multiplied with the initial matrix that was passed in the constructor.
   */
  virtual void setMatrix(const Matrix& matrix);

  /**
   * Resets Matrix to the initial matrix passed in the constructor, discarding all previous
   * transformations.
   */
  virtual void resetMatrix();

  /**
   * Replaces clip with the intersection of clip and rect. The resulting clip is aliased; pixels are
   * fully contained by the clip. The rect is transformed by the current Matrix before it is
   * combined with clip.
   */
  virtual void clipRect(const Rect& rect);

  /**
   * Replaces clip with the intersection of clip and path. The path is transformed by Matrix before
   * it is combined with clip.
   */
  virtual void clipPath(const Path& path);

  /**
   * Draws a rectangle with the specified FillStyle.
   */
  virtual void drawRect(const Rect& rect, const FillStyle& style) = 0;

  /**
   * Draws a rounded rectangle with the specified FillStyle.
   */
  virtual void drawRRect(const RRect& rRect, const FillStyle& style) = 0;

  /**
   * Draws a complex Path with the specified FillStyle and optional Stroke.
   */
  virtual void drawPath(const Path& path, const FillStyle& style, const Stroke* stroke) = 0;

  /**
   * Draws a GlyphRun with the specified FillStyle and optional Stroke.
   */
  virtual void drawGlyphRun(GlyphRun glyphRun, const FillStyle& style, const Stroke* stroke) = 0;

 private:
  MCState state = {};
  std::stack<MCState> stack = {};
};
}  // namespace tgfx
