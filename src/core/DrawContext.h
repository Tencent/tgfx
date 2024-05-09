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
#include "core/MCState.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Picture.h"

namespace tgfx {
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
   * Fills the entire clip region to a transparent color using BlendMode::Src. This has the effect
   * of replacing all pixels within the clip region with a transparent color.
   */
  virtual void clear() = 0;

  /**
   * Draws a rectangle with the specified FillStyle.
   */
  virtual void drawRect(const Rect& rect, const MCState& state, const FillStyle& style) = 0;

  /**
   * Draws a rounded rectangle with the specified FillStyle.
   */
  virtual void drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) = 0;

  /**
   * Draws a complex Path with the specified FillStyle and optional Stroke.
   */
  virtual void drawPath(const Path& path, const MCState& state, const FillStyle& style,
                        const Stroke* stroke) = 0;

  /**
   * Draws a rectangle filled with the specified Image, sampling options, and FillStyle.
   */
  virtual void drawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                             const Rect& rect, const MCState& state, const FillStyle& style) = 0;

  /**
   * Draws a GlyphRun with the specified FillStyle and optional Stroke.
   */
  virtual void drawGlyphRun(GlyphRun glyphRun, const MCState& state, const FillStyle& style,
                            const Stroke* stroke) = 0;

  /**
   * Draws a Picture into an offscreen layer. This offscreen layer is then passed to the image
   * filter (if not nullptr), which generates a new image. This new image is finally drawn using the
   * specified FillStyle.
   */
  virtual void drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                         const FillStyle& style, std::shared_ptr<ImageFilter> filter) = 0;

  /**
   * Draws a Picture.
   */
  virtual void drawPicture(std::shared_ptr<Picture> picture, const MCState& state);
};
}  // namespace tgfx
