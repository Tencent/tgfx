/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <functional>

namespace tgfx {

class LayerViewer {
 public:
  /**
   * Return the Hovered toggle state of layerViewer. When the return value is true, it indicates
   * that the toggle is switched on. The business logic should replace the original left mouse
   * button selection handling with a call to the PickLayer function when the left mouse button
   * is clicked, passing the current mouse coordinates as an argument.
   */
  static bool GetLayerViewerHoveredState();

  /**
   * Set the mouse coordinates to pick the corresponding topmost layer.
   */
  static void PickLayer(float x, float y);
};

}  // namespace tgfx
