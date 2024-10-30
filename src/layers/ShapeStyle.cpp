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

#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
void ShapeStyle::invalidate() {
  for (auto& owner : owners) {
    auto layer = owner.lock();
    if (layer) {
      layer->invalidateContent();
    }
  }
}

void ShapeStyle::attachToLayer(const Layer* layer) {
  owners.push_back(layer->weakThis);
}

void ShapeStyle::detachFromLayer(const Layer* layer) {
  for (auto it = owners.begin(); it != owners.end(); ++it) {
    if (it->lock().get() == layer) {
      owners.erase(it);
      break;
    }
  }
}

}  // namespace tgfx