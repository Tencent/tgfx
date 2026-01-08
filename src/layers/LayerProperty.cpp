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
  for (const auto& owner : owners) {
    owner->invalidateContent();
  }
}

void LayerProperty::invalidateTransform() {
  for (const auto& owner : owners) {
    owner->invalidateTransform();
  }
}

void LayerProperty::attachToLayer(Layer* layer) {
  owners.push_back(layer);
}

void LayerProperty::detachFromLayer(Layer* layer) {
  for (auto it = owners.begin(); it != owners.end(); ++it) {
    if (*it == layer) {
      owners.erase(it);
      break;
    }
  }
}

void LayerProperty::replaceChildProperty(LayerProperty* oldChild, LayerProperty* newChild) {
  if (oldChild) {
    for (const auto& owner : owners) {
      oldChild->detachFromLayer(owner);
    }
  }
  if (newChild) {
    for (const auto& owner : owners) {
      newChild->attachToLayer(owner);
    }
  }
}

}  // namespace tgfx
