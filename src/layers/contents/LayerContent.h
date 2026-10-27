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

#include "tgfx/core/Canvas.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * LayerContent is the base class for all drawable layer contents. It provides a unified interface
 * for rendering geometries with their fill styles.
 */
class LayerContent {
 public:
  virtual ~LayerContent() = default;

  /**
   * Returns the bounds of the content. A geometry no paint captured belongs to the layer's
   * geometry rather than to its painted content, so each query states whether it takes part.
   */
  virtual Rect getBounds(bool includeUnpaintedGeometry) const = 0;

  /**
   * Returns the tight bounds of the content mapped by Matrix. As with getBounds(), the geometries
   * no paint captured take part only when the query asks for them.
   */
  virtual Rect getTightBounds(const Matrix& matrix, bool includeUnpaintedGeometry) const = 0;

  /**
   * Checks if the layer content overlaps or intersects with the specified point (localX, localY).
   * The localX and localY coordinates are in the layer's local coordinate space. This method
   * performs precise hit testing on the actual shape of the layer content. A geometry no paint
   * captured hits only when includeUnpaintedGeometry asks for it; painters answer the rest.
   */
  virtual bool hitTestPoint(float localX, float localY, bool includeUnpaintedGeometry) const = 0;

  /**
   * Draws the contour of the content to the specified canvas. Unlike the rendered content, the
   * contour keeps the painters whose alpha is 0. The geometries no paint captured are drawn only
   * when the query includes them, since a query that excludes them describes what content paints.
   */
  void drawContour(Canvas* canvas, bool antiAlias, bool includeUnpaintedGeometry) const {
    drawContourInternal(canvas, antiAlias, includeUnpaintedGeometry);
  }

  /**
   * Returns true if the contour drawing result equals the opaque content drawing result under the
   * same query. This is true when the content is visible and has no non-opaque non-image shader.
   * The query states which geometries take part, exactly as the drawContour() flag this answer must
   * be paired with: it holds for that one contour, not for every contour the content can draw.
   */
  virtual bool contourEqualsOpaqueContent(bool includeUnpaintedGeometry) const {
    // The default answer takes part in no comparison: a content that does not implement this test
    // must never be reported as matching, whatever the query selects.
    (void)includeUnpaintedGeometry;
    return false;
  }

  /**
   * Returns true if any part of the content uses a blend mode other than SrcOver.
   */
  virtual bool hasBlendMode() const {
    return false;
  }

  /**
   * Returns true if at least one entry of the content carries a paint. A content that holds only
   * geometries no paint captured paints nothing, so it is not "drawable content" even though it
   * exists.
   */
  virtual bool hasPaintedContent() const = 0;

  /**
   * Draws the default content of the layer (content below children) to the specified canvas.
   * Returns true if the content has foreground graphics that should be drawn above children
   * by calling drawForeground().
   */
  virtual bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const = 0;

  /**
   * Draws the foreground content of the layer (content above children) to the specified canvas.
   * This method should only be called if drawDefault() returned true.
   */
  virtual void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const = 0;

 protected:
  /**
   * Draws the contour. A content that holds several painters forwards to them through this channel.
   */
  virtual void drawContourInternal(Canvas* canvas, bool antiAlias,
                                   bool includeUnpaintedGeometry) const = 0;

  enum class Type {
    Rect,
    RRect,
    Path,
    Shape,
    Text,
    Compose,
    Rects,
    RRects,
    Matrix,
    Mesh,
  };

  /**
   * Returns the type of this content.
   */
  virtual Type type() const = 0;

  friend class Types;
};

}  // namespace tgfx
