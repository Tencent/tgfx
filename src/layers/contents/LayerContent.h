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

#include "tgfx/core/BrushModifier.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {
/**
 * LayerContent represents the recorded contents of a layer, which can include default content,
 * foreground content, and the layer's contour. LayerContent is immutable and cannot be changed
 * after it is created.
 */
class LayerContent {
 public:
  virtual ~LayerContent() = default;

  /**
   * Returns the bounds of the content.
   */
  virtual Rect getBounds() const = 0;

  /**
   * Returns the tight bounds of the content mapped by Matrix.
   */
  virtual Rect getTightBounds(const Matrix& matrix) const = 0;

  /**
   * Checks if the layer content overlaps or intersects with the specified point (localX, localY).
   * The localX and localY coordinates are in the layer's local coordinate space. If the
   * shapeHitTest flag is true, it checks the actual shape of the layer content; otherwise, it
   * checks the bounding box.
   */
  virtual bool hitTestPoint(float localX, float localY, bool shapeHitTest) const = 0;

  /**
   * Draws the default content of the layer to the specified canvas using the provided layer fill.
   */
  virtual void drawDefault(Canvas* canvas, const BrushModifier* modifier) const = 0;

  /**
   * Draws the foreground content of the layer to the specified canvas using the provided layer fill.
   */
  virtual void drawForeground(Canvas* canvas, const BrushModifier* modifier) const = 0;

  /**
    * Draws the contour content of the layer to the specified canvas using the provided layer fill.
    */
  virtual void drawContour(Canvas* canvas, const BrushModifier* modifier) const = 0;

 protected:
  enum class Type { Default, Foreground, Contour };

  virtual Type type() const = 0;

  friend class Types;
};
}  // namespace tgfx
