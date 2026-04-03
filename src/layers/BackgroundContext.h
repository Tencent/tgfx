/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "tgfx/core/Surface.h"

namespace tgfx {

class BackgroundContext {
 public:
  static std::shared_ptr<BackgroundContext> Make(Context* context, const Rect& drawRect,
                                                 float maxOutset, float minOutset,
                                                 const Matrix& matrix,
                                                 std::shared_ptr<ColorSpace> colorSpace);

  virtual ~BackgroundContext() = default;

  virtual Canvas* getCanvas() = 0;

  Matrix backgroundMatrix() const;

  std::shared_ptr<Image> getBackgroundImage();

  /**
   * Gets the background image transformed into the specified local coordinate space,
   * scaled by contentScale, and clipped to the given bounds.
   * @param layerBounds The bounds of the target layer in its local space.
   * @param globalMatrix The 2D global transformation matrix of the target layer.
   * @param contentScale The scale factor applied to the layer bounds.
   * @param offset Output: the offset of the returned image relative to the origin.
   * @param dstColorSpace The target color space for the returned image.
   * @return The background image in the layer's local coordinate space, or nullptr on failure.
   */
  std::shared_ptr<Image> getBackgroundImageForLayer(const Rect& layerBounds,
                                                     const Matrix& globalMatrix,
                                                     float contentScale, Point* offset,
                                                     std::shared_ptr<ColorSpace> dstColorSpace);

  std::shared_ptr<BackgroundContext> createSubContext(const Rect& renderBounds,
                                                      bool clipToBackgroundRect);

  void drawToParent(const Paint& paint);

  Rect getBackgroundRect() const {
    return backgroundRect;
  }

 protected:
  virtual std::shared_ptr<Image> onGetBackgroundImage() = 0;

  BackgroundContext(Context* context, const Matrix& matrix, const Rect& rect,
                    std::shared_ptr<ColorSpace> colorSpace)
      : context(context), imageMatrix(matrix), backgroundRect(rect),
        colorSpace(std::move(colorSpace)){};
  Context* context = nullptr;
  Matrix imageMatrix = Matrix::I();
  Rect backgroundRect = Rect::MakeEmpty();
  std::shared_ptr<ColorSpace> colorSpace = nullptr;

  BackgroundContext* parent = nullptr;

  // Offset of this context's surface origin in parent's surface coordinates.
  // Only used when created by createSubContext(renderBounds, ...).
  Point surfaceOffset = Point::Zero();
};

}  // namespace tgfx
