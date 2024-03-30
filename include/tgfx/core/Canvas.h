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
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Font.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
class Surface;
class SurfaceOptions;
class FillStyle;

/**
 * Canvas provides an interface for drawing, and how the drawing is clipped and transformed. Canvas
 * contains a stack of opacity, blend mode, matrix and clip values. Each Canvas draw call transforms
 * the geometry of the object by the concatenation of all matrix values in the stack. The
 * transformed geometry is clipped by the intersection of all the clip values in the stack.
 */
class Canvas {
 public:
  virtual ~Canvas() = default;

  /**
   * Returns the associated Surface if the Canvas is backed by one, otherwise returns nullptr.
   */
  virtual Surface* getSurface() const {
    return nullptr;
  }

  /**
   * Saves matrix and clip. Calling restore() discards changes to them, restoring them to their state
   * when save() was called. Saved Canvas state is put on a stack, multiple calls to save() should
   * be balanced by an equal number of calls to restore().
   */
  void save();

  /**
   * Removes changes to matrix and clip since Canvas state was last saved. The state is removed from
   * the stack. Does nothing if the stack is empty.
   */
  void restore();

  /**
   * Returns the number of saved states. This is the number of times save() has been called minus
   * the number of times restore() has been called. The save count of a new canvas is zero.
   */
  size_t getSaveCount() const;

  /**
   * Restores state to the specified saveCount. If saveCount is greater than the number of saved
   * states, this method does nothing.
   */
  void restoreToCount(size_t saveCount);

  /**
   * Translates the current matrix by dx along the x-axis and dy along the y-axis. Mathematically,
   * it replaces the current matrix with a translation matrix premultiplied with the current matrix.
   * This has the effect of moving the drawing by (dx, dy) before transforming the result with the
   * current matrix.
   */
  void translate(float dx, float dy);

  /**
   * Scales the current matrix by sx along the x-axis and sy along the y-axis. Mathematically, it
   * replaces the current matrix with a scale matrix premultiplied with the current matrix. This has
   * the effect of scaling the drawing by (sx, sy) before transforming the result with the current
   * matrix.
   */
  void scale(float sx, float sy);

  /**
   * Rotates the current matrix by degrees. Positive values rotate the drawing clockwise.
   * Mathematically, it replaces the current matrix with a rotation matrix premultiplied with the
   * current matrix. This has the effect of rotating the drawing by degrees before transforming the
   * result with the current matrix.
   */
  void rotate(float degrees);

  /**
   * Rotates the current matrix by degrees around the point (px, py). Positive values rotate the
   * drawing clockwise. Mathematically, it replaces the current matrix with a rotation matrix
   * premultiplied with the current matrix. This has the effect of rotating the drawing around the
   * point (px, py) by degrees before transforming the result with the current matrix.
   */
  void rotate(float degrees, float px, float py);

  /**
   * Skews the current matrix by sx along the x-axis and sy along the y-axis. A positive value of sx
   * skews the drawing right as y-axis values increase; a positive value of sy skews the drawing
   * down as x-axis values increase. Mathematically, it replaces the current matrix with a skew
   * matrix premultiplied with the current matrix. This has the effect of skewing the drawing by
   * (sx, sy) before transforming the result with the current matrix.
   */
  void skew(float sx, float sy);

  /**
   * Replaces the current Matrix with matrix premultiplied with the existing one. This has the
   * effect of transforming the drawn geometry by matrix, before transforming the result with the
   * existing Matrix.
   */
  void concat(const Matrix& matrix);

  /**
   * Returns the current total matrix.
   */
  const Matrix& getMatrix() const;

  /**
   * Replaces transformation with specified matrix. Unlike concat(), any prior matrix state is
   * overwritten.
   * @param matrix  matrix to copy, replacing existing Matrix
   */
  void setMatrix(const Matrix& matrix);

  /**
   * Sets Matrix to the identity matrix. Any prior matrix state is overwritten.
   */
  void resetMatrix();

  /**
   * Returns the current total clip Path.
   */
  const Path& getTotalClip() const;

  /**
   * Replaces clip with the intersection of clip and rect. The resulting clip is aliased; pixels are
   * fully contained by the clip. The rect is transformed by the current Matrix before it is
   * combined with clip.
   */
  void clipRect(const Rect& rect);

  /**
   * Replaces clip with the intersection of clip and path. The path is transformed by Matrix before
   * it is combined with clip.
   */
  void clipPath(const Path& path);

  /**
   * Fills the entire clip region to a transparent color using BlendMode::Src. This has the effect
   * of replacing all pixels within the clip region with a transparent color.
   */
  void clear();

  /**
   * Fills a specified rectangle with a given color using the current clip and BlendMode::Src. This
   * has the effect of replacing all pixels within the rectangle with the specified color.
   */
  void clearRect(const Rect& rect, const Color& color = Color::Transparent());

  /**
   * Draws a line from (x0, y0) to (x1, y1) using the current clip, matrix, and specified paint.
   * The Paint::style is ignored, as if set to PaintStyle::Stroke.
   */
  void drawLine(float x0, float y0, float x1, float y1, const Paint& paint);

  /**
   * Draws a line from p0 to p1 using the current clip, matrix, and specified paint. The
   * Paint::style is ignored, as if it were set to PaintStyle::Stroke.
   */
  void drawLine(const Point& p0, const Point& p1, const Paint& paint) {
    drawLine(p0.x, p0.y, p1.x, p1.y, paint);
  }

  /**
   * Draws a rectangle using the current clip, matrix, and specified paint.
   */
  void drawRect(const Rect& rect, const Paint& paint);

  /**
   * Draws an oval using the current clip, matrix, and specified paint.
   */
  void drawOval(const Rect& oval, const Paint& paint);

  /**
   * Draws a circle using the current clip, matrix, and specified paint.
   */
  void drawCircle(float centerX, float centerY, float radius, const Paint& paint);

  /**
   * Draws a circle using the current clip, matrix, and specified paint.
   */
  void drawCircle(const Point& center, float radius, const Paint& paint) {
    drawCircle(center.x, center.y, radius, paint);
  }

  /**
   * Draws a round rectangle using the current clip, matrix, and specified paint.
   * @param rect bounds of the round rectangle to draw
   * @param radiusX axis length on x-axis of the rounded corners.
   * @param radiusY axis length on y-axis of the rounded corners.
   * @param paint stroke, blend, color, and so on, used to draw.
   */
  void drawRoundRect(const Rect& rect, float radiusX, float radiusY, const Paint& paint);

  /**
   * Draws a RRect using the current clip, matrix, and specified paint. The rrect may represent a
   * rectangle, circle, oval, rounded rectangle.
   */
  void drawRRect(const RRect& rRect, const Paint& paint);

  /**
   * Draws a path using the current clip, matrix, and specified paint.
   */
  void drawPath(const Path& path, const Paint& paint);

  /**
   * Draws an image, with its top-left corner at (left, top), using current clip, matrix and
   * optional paint. If image->hasMipmaps() is true, it uses FilterMode::Linear and
   * MipmapMode::Linear as the sampling options. Otherwise, it uses FilterMode::Linear and
   * MipmapMode::None as the sampling options.
   */
  void drawImage(std::shared_ptr<Image> image, float left, float top, const Paint* paint = nullptr);

  /**
   * Draws an Image, with its top-left corner at (0, 0), using current clip and matrix premultiplied
   * with existing Matrix. If image->hasMipmaps() is true, it uses FilterMode::Linear and
   * MipmapMode::Linear as the sampling options. Otherwise, it uses FilterMode::Linear and
   * MipmapMode::None as the sampling options.
   */
  void drawImage(std::shared_ptr<Image> image, const Matrix& matrix, const Paint* paint = nullptr);

  /**
   * Draws an image, with its top-left corner at (0, 0), using current clip, matrix and optional
   * paint. If image->hasMipmaps() is true, it uses FilterMode::Linear and MipmapMode::Linear as the
   * sampling options. Otherwise, it uses FilterMode::Linear and MipmapMode::None as the sampling
   * options.
   */
  void drawImage(std::shared_ptr<Image> image, const Paint* paint = nullptr);

  /**
   * Draws an image, with its top-left corner at (0, 0), using current clip, matrix, sampling
   * options and optional paint.
   */
  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const Paint* paint = nullptr);

  /**
   * Draws text, with origin at (x, y), using clip, matrix, font, and paint. The text must be in
   * utf-8 encoding. This function uses the default character-to-glyph mapping from the Typeface in
   * font. It does not perform typeface fallback for characters not found in the Typeface. Glyphs
   * are positioned based on their default advances.
   */
  void drawSimpleText(const std::string& text, float x, float y, const Font& font,
                      const Paint& paint);

  /**
    * Draws an array of glyphs from glyphIDs at positions using clip, matrix, font, and paint.
    * @param glyphs the array of glyphIDs to draw.
    * @param positions where to draw each glyph.
    * @param glyphCount number of glyphs to draw.
    * @param font typeface and size to draw the glyphs.
    * @param paint blend, color, and so on, used to draw.
    */
  void drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                  const Font& font, const Paint& paint);

  /**
   * Draws a set of sprites from the atlas using the current clip, matrix, and specified paint.
   * @param atlas Image containing the sprites.
   * @param matrix Matrix mappings for sprites in atlas.
   * @param tex Rect locations of sprites in the atlas.
   * @param colors one per sprite, may be nullptr.
   * @param count number of sprites to draw.
   * @param sampling SamplingOptions used to sample the atlas image.
   * @param paint blend, alpha, and so on, used to draw.
   */
  void drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                 const Color colors[], size_t count, const SamplingOptions& sampling = {},
                 const Paint* paint = nullptr);

 protected:
  explicit Canvas(const Path& initClip);
  void resetMCState(const Path& initClip);

  virtual void onSave();
  virtual bool onRestore();
  virtual void onTranslate(float dx, float dy);
  virtual void onScale(float sx, float sy);
  virtual void onRotate(float degrees);
  virtual void onSkew(float sx, float sy);
  virtual void onConcat(const Matrix& matrix);
  virtual void onSetMatrix(const Matrix& matrix);
  virtual void onResetMatrix();
  virtual void onClipRect(const Rect& rect);
  virtual void onClipPath(const Path& path);

  virtual void onClear() = 0;
  virtual void onDrawRect(const Rect& rect, const FillStyle& style) = 0;
  virtual void onDrawRRect(const RRect& rRect, const FillStyle& style) = 0;
  virtual void onDrawPath(const Path& path, const FillStyle& style, const Stroke* stroke) = 0;
  virtual void onDrawImageRect(const Rect& rect, std::shared_ptr<Image> image,
                               const SamplingOptions& sampling, const FillStyle& style) = 0;
  virtual void onDrawGlyphRun(GlyphRun glyphRun, const FillStyle& style, const Stroke* stroke) = 0;

 private:
  struct MCState {
    Matrix matrix = Matrix::I();
    Path clip = {};
  };

  MCState state = {};
  std::stack<MCState> stack = {};

  bool drawSimplePath(const Path& path, const FillStyle& style);
  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling, const Paint* paint,
                 const Matrix* extraMatrix);
  void drawImageRect(const Rect& rect, std::shared_ptr<Image> image,
                     const SamplingOptions& sampling, const FillStyle& style,
                     const Matrix& extraMatrix);
};
}  // namespace tgfx
