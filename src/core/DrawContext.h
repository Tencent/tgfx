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

class Surface;

/**
 * DrawContext is an abstract class that provides drawing APIs for rendering graphics primitives.
 */
class DrawContext {
 public:
  virtual ~DrawContext() = default;

  /**
   * Returns the associated Surface if the DrawContext is backed by one, otherwise returns nullptr.
   */
  virtual Surface* getSurface() const {
    return nullptr;
  }

  /**
   * Returns the current total matrix.
   */
  const Matrix& getMatrix() const {
    return state.matrix;
  }

  /**
   * Returns the current total clip Path.
   */
  const Path& getClip() const {
    return state.clip;
  }

  /**
   * Returns the number of saved states. This is the number of times save() has been called minus
   * the number of times restore() has been called.
   */
  size_t getSaveCount() const {
    return stack.size();
  }

  /**
   * Restores state to the specified saveCount. If saveCount is greater than the number of saved
   * states, this method does nothing.
   */
  void restoreToCount(size_t saveCount);

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
  virtual bool restore();

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
   * Fills the entire clip region to a transparent color using BlendMode::Src. This has the effect
   * of replacing all pixels within the clip region with a transparent color.
   */
  virtual void clear() = 0;

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
   * Draws a rectangle filled with the specified Image, sampling options, and FillStyle.
   */
  virtual void drawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                             const Rect& rect, const FillStyle& style) = 0;

  /**
   * Draws a GlyphRun with the specified FillStyle and optional Stroke.
   */
  virtual void drawGlyphRun(GlyphRun glyphRun, const FillStyle& style, const Stroke* stroke) = 0;

 protected:
  explicit DrawContext(const Path& initClip);

  void resetMCState(const Path& initClip);

 private:
  MCState state = {};
  std::stack<MCState> stack = {};
};
}  // namespace tgfx
