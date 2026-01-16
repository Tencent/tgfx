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

#include "LayerSerialization.h"
#include "LayerFilterSerialization.h"
#include "core/utils/Log.h"
#include "tgfx/layers/ImageLayer.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeLayer.h"
#include "tgfx/layers/SolidLayer.h"
#include "tgfx/layers/TextLayer.h"

namespace tgfx {

std::shared_ptr<Data> LayerSerialization::SerializeLayer(
    const Layer* layer, SerializeUtils::ComplexObjSerMap* map,
    SerializeUtils::RenderableObjSerMap* rosMap, tgfx::inspect::LayerTreeMessage type) {
  DEBUG_ASSERT(layer != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, type, startMap, contentMap);
  SerializeBasicLayerImpl(fbb, layer, map, rosMap);
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

std::shared_ptr<Data> LayerSerialization::SerializeTreeNode(
    std::shared_ptr<Layer> layer,
    std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap) {
  flexbuffers::Builder fbb;
  size_t startMap = fbb.StartMap();
  fbb.Key("Type");
  fbb.UInt(static_cast<uint8_t>(tgfx::inspect::LayerTreeMessage::LayerTree));
  fbb.Key("Content");
  SerializeTreeNodeImpl(fbb, layer, layerMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void LayerSerialization::SerializeTreeNodeImpl(
    flexbuffers::Builder& fbb, std::shared_ptr<Layer> layer,
    std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap) {
  auto startMap = fbb.StartMap();
  fbb.Key("LayerType");
  fbb.String(SerializeUtils::LayerTypeToString(layer->type()));
  fbb.Key("Address");
  fbb.UInt(reinterpret_cast<uint64_t>(layer.get()));
  fbb.Key("Children");
  auto startVector = fbb.StartVector();
  std::vector<std::shared_ptr<Layer>> children = layer->children();
  for (const auto& child : children) {
    if (child->name() != HighLightLayerName) {
      SerializeTreeNodeImpl(fbb, child, layerMap);
    }
  }
  fbb.EndVector(startVector, false, false);
  fbb.EndMap(startMap);
  layerMap.insert({reinterpret_cast<uint64_t>(layer.get()), layer});
}

void LayerSerialization::SerializeBasicLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                                 SerializeUtils::ComplexObjSerMap* map,
                                                 SerializeUtils::RenderableObjSerMap* rosMap) {
  SerializeUtils::SetFlexBufferMap(fbb, "type", SerializeUtils::LayerTypeToString(layer->type()));
  SerializeUtils::SetFlexBufferMap(fbb, "visible", layer->visible());
  SerializeUtils::SetFlexBufferMap(fbb, "allowsEdgeAntialiasing", layer->allowsEdgeAntialiasing());
  SerializeUtils::SetFlexBufferMap(fbb, "allowsGroupOpacity", layer->allowsGroupOpacity());
  SerializeUtils::SetFlexBufferMap(fbb, "excludeChildEffectsInLayerStyle",
                                   layer->excludeChildEffectsInLayerStyle());
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(layer->blendMode()));
  SerializeUtils::SetFlexBufferMap(fbb, "name", layer->name());
  SerializeUtils::SetFlexBufferMap(fbb, "alpha", layer->alpha());

  auto matID = SerializeUtils::GetObjID();
  auto matrix = layer->matrix();
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", "", false, true, matID);
  SerializeUtils::FillComplexObjSerMap(matrix, matID, map);

  auto posID = SerializeUtils::GetObjID();
  auto position = layer->position();
  SerializeUtils::SetFlexBufferMap(fbb, "position", "", false, true, posID);
  SerializeUtils::FillComplexObjSerMap(position, posID, map);

  auto filters = layer->filters();
  auto filterSize = static_cast<unsigned int>(filters.size());
  auto filtersID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "filters", filterSize, false, filterSize, filtersID);
  SerializeUtils::FillComplexObjSerMap(filters, filtersID, map);

  auto mask = layer->mask();
  auto maskID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "mask", reinterpret_cast<uint64_t>(mask.get()), true,
                                   mask != nullptr, maskID);
  SerializeUtils::FillComplexObjSerMap(mask, maskID, map, rosMap);

  auto scrollRectID = SerializeUtils::GetObjID();
  auto scrollRect = layer->scrollRect();
  SerializeUtils::SetFlexBufferMap(fbb, "scrollRect", "", false, true, scrollRectID);
  SerializeUtils::FillComplexObjSerMap(scrollRect, scrollRectID, map);

  auto rootID = SerializeUtils::GetObjID();
  std::shared_ptr<Layer> root = layer->root() ? layer->root()->shared_from_this() : nullptr;
  SerializeUtils::SetFlexBufferMap(fbb, "root", reinterpret_cast<uint64_t>(root.get()), true,
                                   root != nullptr, rootID);
  SerializeUtils::FillComplexObjSerMap(root, rootID, map, rosMap);

  auto parentID = SerializeUtils::GetObjID();
  auto parent = layer->parent();
  SerializeUtils::SetFlexBufferMap(fbb, "parent", reinterpret_cast<uint64_t>(parent), true,
                                   parent != nullptr, parentID);
  SerializeUtils::FillComplexObjSerMap(parent ? parent->shared_from_this() : nullptr, parentID, map,
                                       rosMap);

  auto childrenID = SerializeUtils::GetObjID();
  auto children = layer->children();
  auto childrenSize = static_cast<unsigned int>(children.size());
  SerializeUtils::SetFlexBufferMap(fbb, "children", childrenSize, false, childrenSize, childrenID);
  SerializeUtils::FillComplexObjSerMap(children, childrenID, map, rosMap);

  auto layerStylesID = SerializeUtils::GetObjID();
  auto layerStyles = layer->layerStyles();
  auto layerStylesSize = static_cast<unsigned int>(layerStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "layerStyles", layerStylesSize, false, layerStylesSize,
                                   layerStylesID);
  SerializeUtils::FillComplexObjSerMap(layerStyles, layerStylesID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "dirtyContent", layer->bitFields.dirtyContent);
  SerializeUtils::SetFlexBufferMap(fbb, "dirtyDescendents", layer->bitFields.dirtyDescendents);
  SerializeUtils::SetFlexBufferMap(fbb, "dirtyTransform", layer->bitFields.dirtyTransform);

  auto maskOwnerID = SerializeUtils::GetObjID();
  std::shared_ptr<Layer> maskOwner =
      layer->maskOwner ? layer->maskOwner->shared_from_this() : nullptr;
  SerializeUtils::SetFlexBufferMap(fbb, "maskOwner", reinterpret_cast<uint64_t>(maskOwner.get()),
                                   true, maskOwner != nullptr, maskOwnerID);
  SerializeUtils::FillComplexObjSerMap(maskOwner, maskOwnerID, map, rosMap);

  auto renderBoundsID = SerializeUtils::GetObjID();
  auto renderBounds = layer->renderBounds;
  SerializeUtils::SetFlexBufferMap(fbb, "renderBounds", "", false, true, renderBoundsID);
  SerializeUtils::FillComplexObjSerMap(renderBounds, renderBoundsID, map);

  auto recordedContentID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "recordedContent", "", false, (bool)layer->layerContent,
                                   recordedContentID);
  SerializeUtils::FillComplexObjSermap(layer->layerContent.get(), recordedContentID, map, rosMap);
}

}  // namespace tgfx
