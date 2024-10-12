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
   * Returns true if the display list allows layers to perform edge antialiasing. This means the
   * edges of shapes and images can be drawn with partial transparency. The default value is true.
   */
  bool allowsEdgeAntialiasing() const {
    return _allowsEdgeAntialiasing;
  }

  /**
   * Sets whether the display list allows layers to perform edge antialiasing.
   */
  void setAllowsEdgeAntialiasing(bool edgeAntiAlias);

  /**
   * Returns true if the display list allows layers to be composited as a separate group from their
   * parent. When true and the layer’s alpha value is less than 1.0, the layer can composite itself
   * separately from its parent. This ensures correct rendering for layers with multiple opaque
   * components but may reduce performance. The default value is false.
   */
  bool allowsGroupOpacity() const {
    return _allowsGroupOpacity;
  }

  /**
   * Sets whether the display list allows layers to be composited as a separate group from their
   * parent.
   */
  void setAllowsGroupOpacity(bool value);

  /**
   * Returns the root layer of the display list.
   */
  Layer* root() const;

 private:
  bool _allowsEdgeAntialiasing = true;
  bool _allowsGroupOpacity = false;
  std::shared_ptr<Layer> _root = nullptr;
};
}  // namespace tgfx
