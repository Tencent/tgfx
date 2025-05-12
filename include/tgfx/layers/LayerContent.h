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

#include "tgfx/core/Canvas.h"

namespace tgfx {

/**
 * LayerContent represents the content of a layer, such as a shape, image, or text. LayerContent is
 * immutable and cannot be changed after it is created.
 */
class LayerContent {
 public:
  /**
   * Composes the given contents into a single content.
   */
  static std::unique_ptr<LayerContent> Compose(std::vector<std::unique_ptr<LayerContent>> contents);

  virtual ~LayerContent() = default;

  /**
   * Returns the bounds of the content.
   */
  virtual Rect getBounds() const = 0;

  /**
   * Returns the tight bounds of the content.
   */
  virtual Rect getTightBounds() const {
    return getBounds();
  }

  /**
   * Draws the content to the given canvas with the given paint.
   */
  virtual void draw(Canvas* canvas, const Paint& paint) const = 0;

  /**
    * Checks if the layer content overlaps or intersects with the specified point (localX, localY).
    * The localX and localY coordinates are in the layer's local coordinate space. If the
    * pixelHitTest flag is true, it checks the actual pixels of the layer content; otherwise, it
    * checks the bounding box.
  */
  virtual bool hitTestPoint(float localX, float localY, bool pixelHitTest) = 0;

 protected:
  enum class Type {
    LayerContent,
    ComposeContent,
    ImageContent,
    RasterizedContent,
    ShapeContent,
    SolidContent,
    TextContent
  };
  virtual Type type() const {
    return Type::LayerContent;
  }
  friend class Types;
  friend class LayerContentSerialization;
};
}  // namespace tgfx
