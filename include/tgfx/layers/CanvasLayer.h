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
#include "tgfx/layers/Layer.h"

namespace tgfx {
/**
 * CanvasLayer lets you draw custom graphics using the Canvas API. The graphics you create are saved
 * in a Picture object that can be replayed later.
 */
class CanvasLayer : public Layer {
 public:
  /**
   * Creates a new image layer.
   */
  static std::shared_ptr<CanvasLayer> Make();

  LayerType type() const override {
    return LayerType::Canvas;
  }

  /**
   * Returns the canvas used to draw the custom graphics.
   */
  Canvas* getCanvas() const;

 protected:
  CanvasLayer() = default;
};
}  // namespace tgfx
