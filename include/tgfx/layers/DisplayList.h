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

#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
/**
 * DisplayList represents a collection of layers can be drawn to a Surface. Note: All layers in the
 * display list are not thread-safe and should only be accessed from a single thread.
 */
class DisplayList {
 public:
  DisplayList();

  virtual ~DisplayList() = default;

  /**
   * Returns the root layer of the display list. Note: The root layer cannot be added to another
   * layer. Therefore, properties like alpha, blendMode, position, matrix, visibility, scrollRect,
   * and mask have no effect on the root layer since it will never have a parent.
   */
  Layer* root() const;

  /**
   * Draws the display list to the given surface.
   * Returns false if the surface is a nullptr or if the surface does not require an update.
   * @param surface The surface to draw the display list to.
   * @param replaceAll If true, the surface will be cleared before drawing the display list.
   * Otherwise, the display list will be drawn on top of the existing content.
   */
  bool render(Surface* surface, bool replaceAll = true);

 private:
  std::shared_ptr<Layer> _root = nullptr;
  uint32_t surfaceContentVersion = 0u;
  uint32_t surfaceID = 0u;
};
}  // namespace tgfx
