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
#include "tgfx/layers/Layer.h"

namespace tgfx {

class LayerTreeViewer {
 public:
  /**
  * In debug mode, this interface is used to set the layer to be inspected. The corresponding layer
  * will be selected in the Tgfx Inspector tool, displaying its related properties (e.g：it can be
  * set to select the layer at the cursor's position when the left mouse button is clicked). In
  * release mode, the internal implementation is empty and does nothing.
  */
  static void SetSelectedLayer(std::shared_ptr<Layer> layer);
};
}  // namespace tgfx
