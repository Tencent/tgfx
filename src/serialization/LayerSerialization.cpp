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
#ifdef TGFX_USE_INSPECTOR

#include <tgfx/layers/ImageLayer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/SolidLayer.h>
#include <tgfx/layers/TextLayer.h>
#include "LayerFilterSerialization.h"
#include "LayerSerialization.h"
#include "core/utils/Log.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

std::shared_ptr<Data> LayerSerialization::SerializeLayer(const Layer* layer,
                                                         SerializeUtils::MapRef map) {
  DEBUG_ASSERT(layer != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layer->type();
  switch (type) {
    case LayerType::Image:
      SerializeImageLayerImpl(fbb, layer, map);
      break;
    case LayerType::Shape:
      SerializeShapeLayerImpl(fbb, layer, map);
      break;
    case LayerType::Text:
      SerializeTextLayerImpl(fbb, layer, map);
      break;
    case LayerType::Solid:
      SerializeSolidLayerImpl(fbb, layer, map);
      break;
    case LayerType::Layer:
    case LayerType::Gradient:
      SerializeBasicLayerImpl(fbb, layer, map);
      break;
    default:
      LOGE("Unknown layer type!");
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

std::shared_ptr<Data> LayerSerialization::SerializeTreeNode(
    std::shared_ptr<Layer> layer,
    std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap) {
  flexbuffers::Builder fbb;
  size_t startMap = fbb.StartMap();
  fbb.Key("Type");
  fbb.String("LayerTree");
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
    SerializeTreeNodeImpl(fbb, child, layerMap);
  }
  fbb.EndVector(startVector, false, false);
  fbb.EndMap(startMap);
  layerMap.insert({reinterpret_cast<uint64_t>(layer.get()), layer});
}

void LayerSerialization::SerializeBasicLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                                 SerializeUtils::MapRef map) {
  SerializeUtils::SetFlexBufferMap(fbb, "type", SerializeUtils::LayerTypeToString(layer->type()));
  SerializeUtils::SetFlexBufferMap(fbb, "visible", layer->visible());
  SerializeUtils::SetFlexBufferMap(fbb, "shouldRasterize", layer->shouldRasterize());
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
  SerializeUtils::FillMap(matrix, matID, map);

  auto posID = SerializeUtils::GetObjID();
  auto position = layer->position();
  SerializeUtils::SetFlexBufferMap(fbb, "position", "", false, true, posID);
  SerializeUtils::FillMap(position, posID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "rasterizationScale", layer->rasterizationScale());

  auto filters = layer->filters();
  auto filterSize = static_cast<unsigned int>(filters.size());
  auto filtersID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "filters", filterSize, false, filterSize, filtersID);
  SerializeUtils::FillMap(filters, filtersID, map);

  auto mask = layer->mask();
  auto maskID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "mask", reinterpret_cast<uint64_t>(mask.get()), true,
                                   mask != nullptr, maskID);
  SerializeUtils::FillMap(mask, maskID, map);

  auto scrollRectID = SerializeUtils::GetObjID();
  auto scrollRect = layer->scrollRect();
  SerializeUtils::SetFlexBufferMap(fbb, "scrollRect", "", false, true, scrollRectID);
  SerializeUtils::FillMap(scrollRect, scrollRectID, map);

  auto rootID = SerializeUtils::GetObjID();
  std::shared_ptr<Layer> root = layer->_root ? layer->_root->weakThis.lock() : nullptr;
  SerializeUtils::SetFlexBufferMap(fbb, "root", reinterpret_cast<uint64_t>(root.get()), true,
                                   root != nullptr, rootID);
  SerializeUtils::FillMap(root, rootID, map);

  auto parentID = SerializeUtils::GetObjID();
  auto parent = layer->parent();
  SerializeUtils::SetFlexBufferMap(fbb, "parent", reinterpret_cast<uint64_t>(parent.get()), true,
                                   parent != nullptr, parentID);
  SerializeUtils::FillMap(parent, parentID, map);

  auto childrenID = SerializeUtils::GetObjID();
  auto children = layer->children();
  auto childrenSize = static_cast<unsigned int>(children.size());
  SerializeUtils::SetFlexBufferMap(fbb, "children", childrenSize, false, childrenSize, childrenID);
  SerializeUtils::FillMap(children, childrenID, map);

  auto layerStylesID = SerializeUtils::GetObjID();
  auto layerStyles = layer->layerStyles();
  auto layerStylesSize = static_cast<unsigned int>(layerStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "layerStyles", layerStylesSize, false, layerStylesSize,
                                   layerStylesID);
  SerializeUtils::FillMap(layerStyles, layerStylesID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "dirtyContent", layer->bitFields.dirtyContent);
  SerializeUtils::SetFlexBufferMap(fbb, "dirtyDescendents", layer->bitFields.dirtyDescendents);
  SerializeUtils::SetFlexBufferMap(fbb, "dirtyTransform", layer->bitFields.dirtyTransform);
  SerializeUtils::SetFlexBufferMap(fbb, "dirtyBackground", layer->bitFields.dirtyBackground);

  auto maskOwnerID = SerializeUtils::GetObjID();
  std::shared_ptr<Layer> maskOwner = layer->maskOwner ? layer->maskOwner->weakThis.lock() : nullptr;
  SerializeUtils::SetFlexBufferMap(fbb, "maskOwner", reinterpret_cast<uint64_t>(maskOwner.get()),
                                   true, maskOwner != nullptr, maskOwnerID);
  SerializeUtils::FillMap(maskOwner, maskOwnerID, map);

  auto renderBoundsID = SerializeUtils::GetObjID();
  auto renderBounds = layer->renderBounds;
  SerializeUtils::SetFlexBufferMap(fbb, "renderBounds", "", false, true, renderBoundsID);
  SerializeUtils::FillMap(renderBounds, renderBoundsID, map);
}

void LayerSerialization::SerializeImageLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                                 SerializeUtils::MapRef map) {
  SerializeBasicLayerImpl(fbb, layer, map);
  const ImageLayer* imageLayer = static_cast<const ImageLayer*>(layer);

  auto samplingID = SerializeUtils::GetObjID();
  auto sampling = imageLayer->sampling();
  SerializeUtils::SetFlexBufferMap(fbb, "sampling", "", false, true, samplingID);
  SerializeUtils::FillMap(sampling, samplingID, map);

  auto imageID = SerializeUtils::GetObjID();
  auto image = imageLayer->image();
  SerializeUtils::SetFlexBufferMap(fbb, "image", reinterpret_cast<uint64_t>(image.get()), true,
                                   image != nullptr, imageID);
  SerializeUtils::FillMap(image, imageID, map);
}

void LayerSerialization::SerializeShapeLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                                 SerializeUtils::MapRef map) {
  SerializeBasicLayerImpl(fbb, layer, map);
  const ShapeLayer* shapeLayer = static_cast<const ShapeLayer*>(layer);

  auto shapeID = SerializeUtils::GetObjID();
  auto shape = shapeLayer->shape();
  SerializeUtils::SetFlexBufferMap(fbb, "shape", reinterpret_cast<uint64_t>(shape.get()), true,
                                   shape != nullptr, shapeID);
  SerializeUtils::FillMap(shape, shapeID, map);

  auto fillStylesID = SerializeUtils::GetObjID();
  auto fillStyles = shapeLayer->fillStyles();
  auto fillStylesSize = static_cast<unsigned int>(fillStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "fillStyles", fillStylesSize, false, fillStylesSize,
                                   fillStylesID);
  SerializeUtils::FillMap(fillStyles, fillStylesID, map);

  auto strokeStylesID = SerializeUtils::GetObjID();
  auto strokeStyles = shapeLayer->strokeStyles();
  auto strokeStylesSize = static_cast<unsigned int>(strokeStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeStyles", strokeStylesSize, false, strokeStylesSize,
                                   strokeStylesID);
  SerializeUtils::FillMap(strokeStyles, strokeStylesID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "lineCap",
                                   SerializeUtils::LineCapToString(shapeLayer->lineCap()));
  SerializeUtils::SetFlexBufferMap(fbb, "lineJoin",
                                   SerializeUtils::LineJoinToString(shapeLayer->lineJoin()));
  SerializeUtils::SetFlexBufferMap(fbb, "miterLimit", shapeLayer->miterLimit());
  SerializeUtils::SetFlexBufferMap(fbb, "lineWidth", shapeLayer->lineWidth());

  auto lineDashPatternID = SerializeUtils::GetObjID();
  auto lineDashPattern = shapeLayer->lineDashPattern();
  auto lineDashPatternSize = static_cast<unsigned int>(lineDashPattern.size());
  SerializeUtils::SetFlexBufferMap(fbb, "lineDashPattern", lineDashPatternSize, false,
                                   lineDashPatternSize, lineDashPatternID);
  SerializeUtils::FillMap(lineDashPattern, lineDashPatternID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "lineDashPhase", shapeLayer->lineDashPhase());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeStart", shapeLayer->strokeStart());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeEnd", shapeLayer->strokeEnd());
  SerializeUtils::SetFlexBufferMap(fbb, "lineDashAdaptive", shapeLayer->lineDashAdaptive());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeAlign",
                                   SerializeUtils::StrokeAlignToString(shapeLayer->strokeAlign()));
  SerializeUtils::SetFlexBufferMap(fbb, "strokeOnTop", shapeLayer->strokeOnTop());
}

void LayerSerialization::SerializeSolidLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                                 SerializeUtils::MapRef map) {
  SerializeBasicLayerImpl(fbb, layer, map);
  const SolidLayer* solidLayer = static_cast<const SolidLayer*>(layer);

  auto colorID = SerializeUtils::GetObjID();
  auto color = solidLayer->color();
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true, colorID);
  SerializeUtils::FillMap(color, colorID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "width", solidLayer->width());
  SerializeUtils::SetFlexBufferMap(fbb, "height", solidLayer->height());
  SerializeUtils::SetFlexBufferMap(fbb, "radiusX", solidLayer->radiusX());
  SerializeUtils::SetFlexBufferMap(fbb, "radiusY", solidLayer->radiusY());
}

void LayerSerialization::SerializeTextLayerImpl(flexbuffers::Builder& fbb, const Layer* layer,
                                                SerializeUtils::MapRef map) {
  SerializeBasicLayerImpl(fbb, layer, map);
  const TextLayer* textLayer = static_cast<const TextLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "text", textLayer->text());

  auto textColorID = SerializeUtils::GetObjID();
  auto textColor = textLayer->textColor();
  SerializeUtils::SetFlexBufferMap(fbb, "textColor", "", false, true, textColorID);
  SerializeUtils::FillMap(textColor, textColorID, map);

  auto fontID = SerializeUtils::GetObjID();
  auto font = textLayer->font();
  SerializeUtils::SetFlexBufferMap(fbb, "font", "", false, true, fontID);
  SerializeUtils::FillMap(font, fontID, map);

  SerializeUtils::SetFlexBufferMap(fbb, "width", textLayer->width());
  SerializeUtils::SetFlexBufferMap(fbb, "height", textLayer->height());
  SerializeUtils::SetFlexBufferMap(fbb, "textAlign",
                                   SerializeUtils::TextAlignToString(textLayer->textAlign()));
  SerializeUtils::SetFlexBufferMap(fbb, "autoWrap", textLayer->autoWrap());
}
}  // namespace tgfx
#endif
