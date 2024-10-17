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

#include "tgfx/layers/DisplayList.h"

namespace tgfx {

DisplayList::DisplayList() : _root(Layer::Make()) {
  _root->_owner = this;
}

Layer* DisplayList::root() const {
  return _root.get();
}

void DisplayList::draw(Canvas* canvas) {
  _root->draw(canvas);
}

bool DisplayList::hasCache(const Layer* layer) const {
  return surfaceCaches.find(layer->uniqueID) != surfaceCaches.end();
}

std::shared_ptr<Surface> DisplayList::getSurfaceCache(const Layer* layer) {
  if (hasCache(layer)) {
    return surfaceCaches[layer->uniqueID];
  }
  return nullptr;
}

void DisplayList::setSurfaceCache(const Layer* layer, std::shared_ptr<Surface> surface) {
  if (surface == nullptr) {
    if (hasCache(layer)) {
      surfaceCaches.erase(layer->uniqueID);
    }
  } else {
    surfaceCaches[layer->uniqueID] = surface;
  }
}

}  // namespace tgfx