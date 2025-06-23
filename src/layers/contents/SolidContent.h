/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
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
class SolidContent : public LayerContent {
 public:
  SolidContent(const RRect& rRect, const Color& color);

  Rect getBounds() const override {
    return _rRect.rect;
  }

  void draw(Canvas* canvas, const Paint& paint) const override;

  bool hitTestPoint(float localX, float localY, bool shapeHitTest) override;

  RRect _rRect;
  Color _color;

 protected:
  Type type() const override {
    return Type::SolidContent;
  }
};
}  // namespace tgfx
