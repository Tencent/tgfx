/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/layers/LayerProperty.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

void LayerProperty::invalidateContent() {
  for (auto& owner : owners) {
    if (auto layer = owner.lock()) {
      layer->invalidateContent();
    }
  }
}

void LayerProperty::invalidateTransform() {
  for (auto& owner : owners) {
    if (auto layer = owner.lock()) {
      layer->invalidateTransform();
    }
  }
}

void LayerProperty::attachToLayer(Layer* layer) {
  owners.push_back(layer->weak_from_this());
}

void LayerProperty::detachFromLayer(Layer* layer) {
  for (auto owner = owners.begin(); owner != owners.end(); ++owner) {
    if (owner->lock().get() == layer) {
      owners.erase(owner);
      break;
    }
  }
}

}  // namespace tgfx
