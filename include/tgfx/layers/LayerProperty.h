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
#include <vector>

namespace tgfx {
class Layer;

/**
 * A property of a layer that may change the content of the layer.
 */
class LayerProperty {
 public:
  virtual ~LayerProperty() = default;

 protected:
  /**
   * Mark the property as dirty and notify the layer that the content needs to be updated.
   */
  void invalidate();

  bool dirty = true;

 private:
  void attachToLayer(const Layer* layer);

  void detachFromLayer(const Layer* layer);

  std::vector<std::weak_ptr<Layer>> owners;

  friend class Layer;
};

}  // namespace tgfx
