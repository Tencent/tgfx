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
   * Renders the display list onto the given surface.
   * @param surface The surface to render the display list on.
   * @param replaceAll If true, the surface will be cleared before rendering the display list.
   * Otherwise, the display list will be rendered over the existing content.
   * @return True if the surface content was updated, otherwise false.
   */
  bool render(Surface* surface, bool replaceAll = true);

  void serializingLayerTree();

  void pickedLayerAttributeSerialization(float x, float y);

  void pickLayer(std::shared_ptr<Layer> layer);

 private:
  std::shared_ptr<Layer> _root = nullptr;
  uint32_t surfaceContentVersion = 0u;
  uint32_t surfaceID = 0u;

 friend class LayerInspector;
};
}  // namespace tgfx
