/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "tgfx/core/Font.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/svg/SVGExporter.h"

namespace tgfx {
class Surface;
class DrawContext;
class MCState;
class CanvasState;

/**
 * SrcRectConstraint controls the behavior at the edge of source rect, provided to drawImageRect()
 * when there is any filtering. If Strict is set, then extra code is used to ensure it never samples
 * outside the src-rect. Strict disables the use of mipmaps.
*/
enum class SrcRectConstraint {
  /**
   * sample only inside bounds; slower
   */
  Strict,
  /**
   * sample outside bounds; faster
   */
  Fast,
};

/**
 * Canvas provides an interface for drawing, including how the drawing is clipped and transformed.
 * It maintains a stack of matrix and clip values. Each draw call on the Canvas transforms the
 * geometry of the object by combining all the matrix values in the stack. The transformed geometry
 * is then clipped by the intersection of all the clip values in the stack.
 */
class Canvas {
 public:
  /**
   * Returns the associated Surface if the Canvas has one, otherwise returns nullptr.
   */
  Surface* getSurface() const {
    return surface;
  }

  /**
   * Saves the current matrix and clip state. Calling restore() will revert any changes made to
   * them, returning them to their state when save() was called. The saved Canvas state is pushed
   * onto a stack, so multiple calls to save() should be matched with an equal number of calls to
   * restore(). Use restoreToCount() with the returned depth to restore this and any subsequent
   * saves.
   * @return the depth of previously saved stack states.
   */
  int save();

  /**
   * Saves the current matrix and clip state, and allocates an offscreen layer for subsequent
   * drawing. When restore() is called, it discards changes to the matrix and clip, and draws the
   * offscreen layer as if it were an Image with the specified Paint. The optional paint applies
   * alpha, ColorFilter, ImageFilter, MaskFilter, and BlendMode when restore() is called. Use
   * restoreToCount() with the returned depth to restore this and any subsequent saves.
   * @param paint the Paint to apply when drawing the offscreen layer.
   * @return the depth of the previously saved stack states.
   */
  int saveLayer(const Paint* paint);

  /**
   * Saves the current matrix and clip state, and allocates an offscreen layer for subsequent
   * drawing. When restore() is called, it discards changes to the matrix and clip, and draws the
   * offscreen layer with the specified alpha. Use restoreToCount() with the returned depth to
   * restore this and any subsequent saves.
   * @param alpha the alpha value to apply when drawing the offscreen layer.
   * @return the depth of the previously saved stack states.
   */
  int saveLayerAlpha(float alpha);

  /**
   * Reverts changes to the matrix and clip made since the last save. Removes the state from the
   * stack. If the stack is empty, it does nothing.
   */
  void restore();

  /**
   * Returns the number of saved stack states. This is the difference between the number of times
   * save() and restore() have been called. A new canvas starts with a save count of zero.
   */
  int getSaveCount() const;

  /**
   * Restores state to the specified saveCount. If saveCount is greater than the number of saved
   * states, this method does nothing.
   * @param saveCount  the save count to restore to.
   */
  void restoreToCount(int saveCount);

  /**
   * Translates the current matrix by dx along the x-axis and dy along the y-axis. This means the
   * current matrix is replaced with a translation matrix that is premultiplied with the current
   * matrix. As a result, the drawing is moved by (dx, dy) before being transformed by the current
   * matrix.
   * @param dx  the distance to translate along the x-axis.
   * @param dy  the distance to translate along the y-axis.
   */
  void translate(float dx, float dy);

  /**
   * Scales the current matrix by sx along the x-axis and sy along the y-axis. This replaces the
   * current matrix with a scale matrix premultiplied with the current matrix, effectively scaling
   * the drawing by (sx, sy) before applying the current matrix transformation.
   * @param sx  the scale factor along the x-axis.
   * @param sy  the scale factor along the y-axis.
   */
  void scale(float sx, float sy);

  /**
   * Rotates the current matrix by the specified degrees. Positive values rotate the drawing
   * clockwise. This replaces the current matrix with a rotation matrix premultiplied with the
   * existing matrix. As a result, the drawing is rotated by the specified degrees before being
   * transformed by the current matrix.
   * @param degrees  the angle of rotation in degrees.
   */
  void rotate(float degrees);

  /**
   * Rotates the current matrix by the specified degrees around the point (px, py). Positive values
   * rotate the drawing clockwise. This replaces the current matrix with a rotation matrix
   * premultiplied with the existing matrix. As a result, the drawing is rotated around the point
   * (px, py) by the specified degrees before being transformed by the current matrix.
   * @param degrees  the angle of rotation in degrees.
   * @param px  the x-coordinate of the point to rotate around.
   * @param py  the y-coordinate of the point to rotate around.
   */
  void rotate(float degrees, float px, float py);

  /**
   * Skews the current matrix by sx along the x-axis and sy along the y-axis. A positive sx skews
   * the drawing to the right as the y-axis values increase; a positive sy skews the drawing
   * downward as the x-axis values increase. This replaces the current matrix with a skew matrix
   * premultiplied with the existing matrix, effectively skewing the drawing by (sx, sy) before
   * applying the current matrix transformation.
   * @param sx  the skew factor along the x-axis.
   * @param sy  the skew factor along the y-axis.
   */
  void skew(float sx, float sy);

  /**
   * Multiplies the current matrix with the given matrix. This transforms the drawn geometry by the
   * given matrix first, followed by the current matrix.
   * @param matrix  the matrix to multiply with the current matrix.
   */
  void concat(const Matrix& matrix);

  /**
   * Returns the current total matrix.
   */
  const Matrix& getMatrix() const;

  /**
   * Replaces the current transformation with the specified matrix. Unlike concat(), this overwrites
   * any existing matrix state.
   * @param matrix  the matrix to copy, replacing the existing matrix
   */
  void setMatrix(const Matrix& matrix);

  /**
   * Resets the matrix to the identity matrix, overwriting any previous matrix state.
   */
  void resetMatrix();

  /**
   * Returns the current total clip Path.
   */
  const Path& getTotalClip() const;

  /**
   * Replaces the current clip with the intersection of the clip and the rectangle. The resulting
   * clip is aliased, meaning pixels are fully contained by the clip. The rectangle is transformed
   * by the current matrix before being combined with the clip.
   * @param rect  the rectangle to clip to.
   */
  void clipRect(const Rect& rect);

  /**
   * Replaces the current clip with the intersection of the clip and the path. The path is
   * transformed by the Matrix before being combined with the clip.
   * @param path  the path to clip to.
   */
  void clipPath(const Path& path);

  /**
   * Fills the current clip with the specified color, using BlendMode::Src. This replaces all pixels
   * within the clip with the specified color.
   */
  void clear(const Color& color = Color::Transparent());

  /**
   * Fills the current clip with the specified color. The color is blended with the destination
   * pixels using the specified blend mode.
   */
  void drawColor(const Color& color, BlendMode blendMode = BlendMode::SrcOver);

  /**
   * Fills the current clip with the specified paint. The paint's shader, color, blend mode, color
   * filter, mask filter, and image filter are applied during the fill. Other paint properties are
   * ignored.
   * @param paint The paint to use for filling.
   */
  void drawPaint(const Paint& paint);

  /**
   * Draws a line from (x0, y0) to (x1, y1) using the current clip, matrix, and specified paint.
   * The Paint::style is always treated as PaintStyle::Stroke.
   * @param x0  the x-coordinate of the start point.
   * @param y0  the y-coordinate of the start point.
   * @param x1  the x-coordinate of the end point.
   * @param y1  the y-coordinate of the end point.
   * @param paint  stroke, blend, color, and so on, used to draw.
   */
  void drawLine(float x0, float y0, float x1, float y1, const Paint& paint);

  /**
   * Draws a line from p0 to p1 using the current clip, matrix, and specified paint. The
   * Paint::style is always treated as PaintStyle::Stroke.
   * @param p0  the start point.
   * @param p1  the end point.
   * @param paint  stroke, blend, color, and so on, used to draw.
   */
  void drawLine(const Point& p0, const Point& p1, const Paint& paint) {
    drawLine(p0.x, p0.y, p1.x, p1.y, paint);
  }

  /**
   * Draws a rectangle using the current clip, matrix, and specified paint.
   * @param rect  the rectangle to draw.
   * @param paint  the paint to use for stroke, blend, color, etc.
   */
  void drawRect(const Rect& rect, const Paint& paint);

  /**
   * Draws an oval using the current clip, matrix, and specified paint.
   * @param oval  the bounds of the oval to draw.
   * @param paint  the paint to use for stroke, blend, color, etc.
   */
  void drawOval(const Rect& oval, const Paint& paint);

  /**
   * Draws a circle using the current clip, matrix, and specified paint.
   * @param centerX  the x-coordinate of the circle's center.
   * @param centerY  the y-coordinate of the circle's center.
   * @param radius  the radius of the circle.
   * @param paint  the paint to use for stroke, blend, color, etc.
   */
  void drawCircle(float centerX, float centerY, float radius, const Paint& paint);

  /**
   * Draws a circle using the current clip, matrix, and specified paint.
   * @param center  the center of the circle.
   * @param radius  the radius of the circle.
   * @param paint  the paint to use for stroke, blend, color, etc.
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
   * Draws an RRect using the current clip, matrix, and specified paint. The RRect can represent a
   * rectangle, circle, oval, or rounded rectangle.
   * @param rRect  the rounded rectangle to draw.
   * @param paint  the paint to use for stroke, blend, color, etc.
   */
  void drawRRect(const RRect& rRect, const Paint& paint);

  /**
   * Draws a path using the current clip, matrix, and specified paint.
   * @param path  the path to draw.
   * @param paint  the paint to use for stroke, blend, color, etc.
   */
  void drawPath(const Path& path, const Paint& paint);

  /**
   * Draws a shape using the current clip, matrix, and specified paint.
   * @param shape  the shape to draw.
   * @param paint  the paint to use for stroke, blend, color, etc.
   */
  void drawShape(std::shared_ptr<Shape> shape, const Paint& paint);

  /**
   * Draws an image with its top-left corner at (0, 0) using the current clip and matrix.
   * Uses the default sampling option: FilterMode::Linear and MipmapMode::Linear.
   * @param image  The image to draw.
   * @param paint  Optional paint for blending, filtering, etc.; can be nullptr.
   */
  void drawImage(std::shared_ptr<Image> image, const Paint* paint = nullptr) {
    drawImage(std::move(image), {}, paint);
  }

  /**
   * Draws an image with its top-left corner at (left, top) using the current clip and matrix.
   * Uses the default sampling option: FilterMode::Linear and MipmapMode::Linear.
   * @param image  The image to draw.
   * @param left  The x-coordinate for the image's top-left corner. This does not affect the paint.
   * @param top  The y-coordinate for the image's top-left corner. This does not affect the paint.
   * @param paint  Optional paint for blending, filtering, etc.; can be nullptr.
   */
  void drawImage(std::shared_ptr<Image> image, float left, float top,
                 const Paint* paint = nullptr) {
    drawImage(std::move(image), left, top, {}, paint);
  }

  /**
   * Draws an image with its top-left corner at (0, 0), using the current clip, matrix, sampling
   * options, and optional paint.
   * @param image  the image to draw.
   * @param sampling  the sampling options used to sample the image.
   * @param paint  the paint to apply blending, filtering, etc.; can be nullptr.
   */
  void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                 const Paint* paint = nullptr);

  /**
   * Draws an image with its top-left corner at (left, top), using the current clip, matrix,
   * sampling options, and optional paint.
   * @param image  The image to draw.
   * @param left The x-coordinate for the image's top-left corner. This does not affect the paint.
   * @param top The y-coordinate for the image's top-left corner. This does not affect the paint.
   * @param sampling  the sampling options used to sample the image.
   * @param paint The paint to apply blending, filtering, etc.; can be nullptr.
   */
  void drawImage(std::shared_ptr<Image> image, float left, float top,
                 const SamplingOptions& sampling, const Paint* paint = nullptr);

  /**
   * Draws an image into a rectangle defined by the destination rectangle (dst) using the current
   * clip, matrix, and specified sampling options.
   * @param image the image to draw.
   * @param dstRect the destination rectangle where the sampled portion will be drawn.
   * @param sampling the sampling options used to sample the image. Defaults to
   * FilterMode::Linear and MipmapMode::Linear.
   * @param paint the paint to apply blending, filtering, etc.; can be nullptr.
   */
  void drawImageRect(std::shared_ptr<Image> image, const Rect& dstRect,
                     const SamplingOptions& sampling = {}, const Paint* paint = nullptr);

  /**
   * Draws a portion of an image defined by the source rectangle (src) into a destination rectangle
   * (dst) using the current clip, matrix, and specified sampling options.
   * @param image  the image to draw.
   * @param srcRect  the source rectangle in the image to sample from.
   * @param dstRect  the destination rectangle where the sampled portion will be drawn.
   * @param sampling  the sampling options used to sample the image. Defaults to
   * FilterMode::Linear and MipmapMode::Linear.
   * @param paint  the paint to apply blending, filtering, etc.; can be nullptr.
   * @param constraint  the constraint for the source rectangle sampling. Defaults to
   * SrcRectConstraint::Fast.
   */
  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling = {}, const Paint* paint = nullptr,
                     SrcRectConstraint constraint = SrcRectConstraint::Fast);

  /**
   * Draws text at the specified (x, y) coordinates using the current clip, matrix, font, and paint.
   * The text must be in UTF-8 encoding. This function uses the default character-to-glyph mapping
   * from the Typeface in the font and does not perform typeface fallback for missing characters.
   * Glyphs are positioned based on their default advances.
   * @param text  the text to draw.
   * @param x  the x-coordinate of the text.
   * @param y  the y-coordinate of the text.
   * @param font  the font to use for drawing.
   * @param paint  the paint to use for blending, coloring, etc.
   */
  void drawSimpleText(const std::string& text, float x, float y, const Font& font,
                      const Paint& paint);

  /**
   * Draws an array of glyphs at specified positions using the current clip, matrix, font, and paint.
   * @param glyphs the array of glyph IDs to draw.
   * @param positions the positions where each glyph should be drawn.
   * @param glyphCount the number of glyphs to draw.
   * @param font the font to use for drawing.
   * @param paint The paint to use for blending, coloring, etc.
   */
  void drawGlyphs(const GlyphID glyphs[], const Point positions[], size_t glyphCount,
                  const Font& font, const Paint& paint);

  /**
   * Draws a TextBlob at the specified (x, y) coordinates using the current clip, matrix, and paint.
   * @param textBlob The TextBlob to draw.
   * @param x The horizontal offset to apply to the TextBlob.
   * @param y The vertical offset to apply to the TextBlob.
   * @param paint The paint to use for blending, coloring, etc.
   */
  void drawTextBlob(std::shared_ptr<TextBlob> textBlob, float x, float y, const Paint& paint);

  /**
   * Draws a Picture using the current clip and matrix. The clip and matrix remain unchanged by the
   * picture's contents, as if save() was called before and restore() was called after drawPicture().
   * @param picture The recorded drawing commands to play back.
   */
  void drawPicture(std::shared_ptr<Picture> picture);

  /**
   * Draws a Picture using the current clip and matrix, combined with the existing Matrix. If paint
   * is provided, the picture is always drawn into a temporary layer before being rendered on the
   * canvas. Note that drawing into a layer can alter its appearance if there are any
   * non-associative blendModes within the picture elements.
   * @param picture The recorded drawing commands to play back.
   * @param matrix The matrix to rotate, scale, translate, etc.; can be nullptr.
   * @param paint The paint to apply transparency, filtering, etc.; can be nullptr.
   */
  void drawPicture(std::shared_ptr<Picture> picture, const Matrix* matrix, const Paint* paint);

  /**
   * Draws multiple sprites from the atlas using the current clip, matrix, and specified paint.
   * @param atlas The image containing the sprites.
   * @param matrix The matrix transformations for the sprites in the atlas.
   * @param tex The rectangle locations of the sprites in the atlas.
   * @param colors An array of colors for each sprite; can be nullptr.
   * @param count The number of sprites to draw.
   * @param sampling The sampling options used to sample the atlas image.
   * @param paint The paint used for blending, alpha, etc.
   */
  void drawAtlas(std::shared_ptr<Image> atlas, const Matrix matrix[], const Rect tex[],
                 const Color colors[], size_t count, const SamplingOptions& sampling = {},
                 const Paint* paint = nullptr);

 private:
  DrawContext* drawContext = nullptr;
  Surface* surface = nullptr;
  bool optimizeMemoryForLayer = false;
  std::unique_ptr<MCState> mcState;
  std::stack<std::unique_ptr<CanvasState>> stateStack;

  explicit Canvas(DrawContext* drawContext, Surface* surface = nullptr,
                  bool optimizeMemoryForLayer = false);
  void drawPath(const Path& path, const MCState& state, const Fill& fill,
                const Stroke* stroke) const;
  void drawImage(std::shared_ptr<Image> image, const Fill& fill, const SamplingOptions& sampling,
                 const Matrix* dstMatrix);
  void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                     const SamplingOptions& sampling, const Fill& fill,
                     SrcRectConstraint constraint = SrcRectConstraint::Fast);
  void drawLayer(std::shared_ptr<Picture> picture, const MCState& state, const Fill& fill,
                 std::shared_ptr<ImageFilter> imageFilter = nullptr);
  void drawFill(const MCState& state, const Fill& fill) const;
  void resetStateStack();

  friend class Surface;
  friend class Picture;
  friend class PictureRecorder;
  friend class SVGExporter;
  friend class PDFDocumentImpl;
  friend class PDFShader;
  friend class PDFExportContext;
  friend class PDFFont;
  friend class Layer;
};

/**
 * AutoCanvasRestore is a helper class that automatically saves the current state of a Canvas when
 * created and restores it when destroyed. This is useful for ensuring that the Canvas state is
 * restored to its previous state when exiting a scope.
 */
class AutoCanvasRestore {
 public:
  /**
   * Creates an AutoSaveRestore object for the specified Canvas. The current state of the Canvas is
   * saved when created and restored when destroyed.
   * @param canvas  the Canvas to save and restore.
   */
  explicit AutoCanvasRestore(Canvas* canvas) : canvas(canvas) {
    if (canvas) {
      saveCount = canvas->save();
    }
  }

  /**
   * Restores the Canvas state to the saved state.
   */
  ~AutoCanvasRestore() {
    if (canvas) {
      canvas->restoreToCount(saveCount);
    }
  }

 private:
  Canvas* canvas = nullptr;
  int saveCount = 0;
};
}  // namespace tgfx
