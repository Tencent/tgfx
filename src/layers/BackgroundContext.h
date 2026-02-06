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
        colorSpace(std::move(colorSpace)) {};
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
