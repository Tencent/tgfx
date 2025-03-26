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

#include "core/utils/Log.h"
#include "layers/DrawArgs.h"
#include "layers/serialization/LayerSerialization.h"
#include "layers/generate/SerializationStructure_generated.h"
#include "core/utils/Profiling.h"

namespace tgfx {

DisplayList::DisplayList() : _root(Layer::Make()) {
  _root->_root = _root.get();
}

Layer* DisplayList::root() const {
  return _root.get();
}

bool DisplayList::render(Surface* surface, bool replaceAll) {
  if (!surface ||
      (replaceAll && surface->uniqueID() == surfaceID &&
       surface->contentVersion() == surfaceContentVersion && !_root->bitFields.childrenDirty)) {
    return false;
  }
  auto canvas = surface->getCanvas();
  if (replaceAll) {
    canvas->clear();
  }
  DrawArgs args(surface->getContext(), surface->renderFlags(), true);
  _root->drawLayer(args, canvas, 1.0f, BlendMode::SrcOver);
  surfaceContentVersion = surface->contentVersion();
  surfaceID = surface->uniqueID();
  return true;
}

void DisplayList::serializingLayerTree() {
  // auto blob = LayerSerialization::serializingTreeNode(_root);
  // // websocket send
  // printf("blob size %zu", blob.size());
  // LAYER_DATA(blob);
}

void DisplayList::pickedLayerAttributeSerialization(float x, float y) {
  std::shared_ptr<Layer> pickedLayer = _root->getLayersUnderPoint(x, y).front();
  if(!pickedLayer) return;

  auto blob = LayerSerialization::serializingLayerAttribute(pickedLayer);
  // websocket send
  LAYER_DATA(blob);
}

void DisplayList::pickLayer(std::shared_ptr<Layer> layer) {
  if(!layer) return;

  auto blob = LayerSerialization::serializingTreeNode(_root);
  // websocket send
  printf("blob size %zu", blob.size());
  LAYER_DATA(blob);

  auto blob1 = LayerSerialization::serializingLayerAttribute(layer);
  // websocket send
  LAYER_DATA(blob1);
}


}  // namespace tgfx
