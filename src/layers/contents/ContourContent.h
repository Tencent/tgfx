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
#include "tgfx/layers/LayerContent.h"

namespace tgfx {

class ContourContent : public LayerContent {
 public:
  explicit ContourContent(std::shared_ptr<Shape> shape);

  Rect getBounds() const override {
    return Rect::MakeEmpty();
  }

  void draw(Canvas*, const Paint&) const override {
  }

  void drawContour(Canvas* canvas, const Paint& paint) const override;

  bool hitTestPoint(float, float, bool) override {
    return false;
  }

 private:
  std::shared_ptr<Shape> shape = nullptr;
};

}  // namespace tgfx