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
#include "core/ClipStack.h"
#include "tgfx/core/Brush.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Mesh.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/SrcRectConstraint.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class Surface;
class TextBlob;

/**
 * DrawContext is an abstract class that provides drawing APIs for rendering graphics primitives.
 */
class DrawContext {
 public:
  virtual ~DrawContext() = default;

  /**
   * Fills the entire clip area with the specified Brush.
   */
  virtual void drawFill(const Brush& brush) = 0;

  /**
   * Draws a rectangle with the specified matrix, clip, and brush.
   * @param rect The rectangle to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   * @param stroke Optional stroke style, nullptr for fill only.
   */
  virtual void drawRect(const Rect& rect, const Matrix& matrix, const ClipStack& clip,
                        const Brush& brush, const Stroke* stroke) = 0;

  /**
   * Draws a rounded rectangle with the specified matrix, clip, and brush.
   * @param rRect The rounded rectangle to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   * @param stroke Optional stroke style, nullptr for fill only.
   */
  virtual void drawRRect(const RRect& rRect, const Matrix& matrix, const ClipStack& clip,
                         const Brush& brush, const Stroke* stroke) = 0;

  /**
   * Draws a path with the specified matrix, clip, and brush.
   * @param path The path to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   */
  virtual void drawPath(const Path& path, const Matrix& matrix, const ClipStack& clip,
                        const Brush& brush) = 0;

  /**
   * Draws a complex Shape with the specified matrix, clip, and brush.
   * @param shape The shape to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   * @param stroke Optional stroke style, nullptr for fill only.
   */
  virtual void drawShape(std::shared_ptr<Shape> shape, const Matrix& matrix, const ClipStack& clip,
                         const Brush& brush, const Stroke* stroke) = 0;

  /**
   * Draws a Mesh with the specified matrix, clip, and brush.
   * @param mesh The mesh to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   */
  virtual void drawMesh(std::shared_ptr<Mesh> mesh, const Matrix& matrix, const ClipStack& clip,
                        const Brush& brush) = 0;

  /**
   * Draws an image with the specified sampling options, matrix, clip, and brush.
   * @param image The image to draw.
   * @param sampling The sampling options for the image.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   */
  virtual void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                         const Matrix& matrix, const ClipStack& clip, const Brush& brush) = 0;

  /**
   * Draws a rectangle filled with the specified image.
   * @param image The image to draw.
   * @param srcRect The source rectangle within the image.
   * @param dstRect The destination rectangle to draw into.
   * @param sampling The sampling options for the image.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   * @param constraint The constraint for source rectangle sampling.
   */
  virtual void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                             const SamplingOptions& sampling, const Matrix& matrix,
                             const ClipStack& clip, const Brush& brush,
                             SrcRectConstraint constraint) = 0;

  /**
   * Draws a TextBlob with the specified matrix, clip, brush, and optional stroke.
   * @param textBlob The text blob to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   * @param stroke Optional stroke style, nullptr for fill only.
   */
  virtual void drawTextBlob(std::shared_ptr<TextBlob> textBlob, const Matrix& matrix,
                            const ClipStack& clip, const Brush& brush, const Stroke* stroke) = 0;

  /**
   * Draws a Picture with the specified matrix and clip.
   * @param picture The picture to draw.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   */
  virtual void drawPicture(std::shared_ptr<Picture> picture, const Matrix& matrix,
                           const ClipStack& clip) = 0;

  /**
   * Draws a Picture into an offscreen layer. This offscreen layer is then passed to the image
   * filter (if not nullptr), which generates a new image. This new image is finally drawn using the
   * specified brush.
   * @param picture The picture to draw.
   * @param filter The image filter to apply, can be nullptr.
   * @param matrix The transformation matrix.
   * @param clip The clip stack containing all clip elements.
   * @param brush The brush for fill style.
   */
  virtual void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                         const Matrix& matrix, const ClipStack& clip, const Brush& brush) = 0;
};
}  // namespace tgfx
