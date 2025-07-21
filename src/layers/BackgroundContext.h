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
                                                 const Matrix& matrix);

  Canvas* getCanvas() const {
    return surface->getCanvas();
  }

  std::shared_ptr<Image> getBackgroundImage() const;

  Matrix backgroundMatrix() const {
    return imageMatrix;
  }

  std::shared_ptr<BackgroundContext> createSubContext() const;

  void drawToParent(const Matrix& paintMatrix, const Paint& paint);

 private:
  BackgroundContext() = default;
  std::shared_ptr<Surface> surface = nullptr;
  Matrix imageMatrix = Matrix::I();
  const BackgroundContext* parent = nullptr;
};

}  // namespace tgfx
