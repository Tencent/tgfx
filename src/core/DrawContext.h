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
#include "core/CanvasState.h"
#include "core/GlyphRunList.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Fill.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
class Surface;

/**
 * DrawContext is an abstract class that provides drawing APIs for rendering graphics primitives.
 */
class DrawContext {
 public:
  virtual ~DrawContext() = default;

  /**
   * Fills the entire clip area with the specified Fill.
   */
  virtual void drawFill(const Fill& fill) = 0;

  /**
   * Draws a rectangle with the specified MCState and Fill.
   */
  virtual void drawRect(const Rect& rect, const MCState& state, const Fill& fill,
                        const Stroke* stroke) = 0;

  /**
   * Draws a rounded rectangle with the specified MCState and Fill.
   */
  virtual void drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                         const Stroke* stroke) = 0;

  /**
   * Draws a path with the specified MCState and Fill.
   */
  virtual void drawPath(const Path& path, const MCState& state, const Fill& fill) = 0;

  /**
   * Draws a complex Shape with the specified MCState and Fill.
   */
  virtual void drawShape(std::shared_ptr<Shape> shape, const MCState& state, const Fill& fill,
                         const Stroke* stroke) = 0;

  /**
   * Draws a image with the specified SamplingOptions, MCState, Fill.
   */
  virtual void drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                         const MCState& state, const Fill& fill) = 0;

  /**
   * Draws a rectangle filled with the specified Image, SamplingOptions, MCState, and Fill.
   */
  virtual void drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect, const Rect& dstRect,
                             const SamplingOptions& sampling, const MCState& state,
                             const Fill& fill, SrcRectConstraint constraint) = 0;

  /**
   * Draws a GlyphRunList with the specified MCState, Fill, and optional Stroke.
   */
  virtual void drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList, const MCState& state,
                                const Fill& fill, const Stroke* stroke) = 0;

  /**
   * Draws a Picture with the specified MCState.
   */
  virtual void drawPicture(std::shared_ptr<Picture> picture, const MCState& state) = 0;

  /**
   * Draws a Picture into an offscreen layer. This offscreen layer is then passed to the image
   * filter (if not nullptr), which generates a new image. This new image is finally drawn using the
   * specified Fill.
   */
  virtual void drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                         const MCState& state, const Fill& fill) = 0;
};
}  // namespace tgfx
