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

#include "LayerSerialization.h"
#include <tgfx/layers/ImageLayer.h>
#include <tgfx/layers/ShapeLayer.h>
#include <tgfx/layers/SolidLayer.h>
#include <tgfx/layers/TextLayer.h>
#include "LayerFilterSerialization.h"
#include "core/utils/Log.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

std::shared_ptr<Data> LayerSerialization::SerializeLayer(Layer* layer) {
  DEBUG_ASSERT(layer != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layer->type();
  switch (type) {
    case LayerType::Image:
      SerializeImageLayerImpl(fbb, layer);
      break;
    case LayerType::Shape:
      SerializeShapeLayerImpl(fbb, layer);
      break;
    case LayerType::Text:
      SerializeTextLayerImpl(fbb, layer);
      break;
    case LayerType::Solid:
      SerializeSolidLayerImpl(fbb, layer);
      break;
    case LayerType::Layer:
    case LayerType::Gradient:
      SerializeBasicLayerImpl(fbb, layer);
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

void LayerSerialization::SerializeBasicLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
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
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "position", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "rasterizationScale", layer->rasterizationScale());
  auto filters = layer->filters();
  auto filterSize = static_cast<unsigned int>(filters.size());
  SerializeUtils::SetFlexBufferMap(fbb, "filters", filterSize, false, filterSize);
  auto mask = layer->mask();
  SerializeUtils::SetFlexBufferMap(fbb, "mask", reinterpret_cast<uint64_t>(mask.get()), true,
                                   mask != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "scrollRect", "", false, true);
  auto root = layer->root();
  SerializeUtils::SetFlexBufferMap(fbb, "root", reinterpret_cast<uint64_t>(root), true,
                                   root != nullptr);
  auto parent = layer->parent();
  SerializeUtils::SetFlexBufferMap(fbb, "parent", reinterpret_cast<uint64_t>(parent.get()), true,
                                   parent != nullptr);
  auto children = layer->children();
  auto childrenSize = static_cast<unsigned int>(children.size());
  SerializeUtils::SetFlexBufferMap(fbb, "children", childrenSize, false, childrenSize);
  auto layerStyles = layer->layerStyles();
  auto layerStylesSize = static_cast<unsigned int>(layerStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "layerStyles", layerStylesSize, false, layerStylesSize);
}
void LayerSerialization::SerializeImageLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  ImageLayer* imageLayer = static_cast<ImageLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "sampling", "", false, true);
  auto image = imageLayer->image();
  SerializeUtils::SetFlexBufferMap(fbb, "image", reinterpret_cast<uint64_t>(image.get()), true,
                                   image != nullptr);
}
void LayerSerialization::SerializeShapeLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  ShapeLayer* shapeLayer = static_cast<ShapeLayer*>(layer);
  auto shape = shapeLayer->shape();
  SerializeUtils::SetFlexBufferMap(fbb, "shape", reinterpret_cast<uint64_t>(shape.get()), true,
                                   shape != nullptr);
  auto fillStyles = shapeLayer->fillStyles();
  auto fillStylesSize = static_cast<unsigned int>(fillStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "fillStyles", fillStylesSize, false, fillStylesSize);
  auto strokeStyles = shapeLayer->strokeStyles();
  auto strokeStylesSize = static_cast<unsigned int>(strokeStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeStyles", strokeStylesSize, false, strokeStylesSize);
  SerializeUtils::SetFlexBufferMap(fbb, "lineCap",
                                   SerializeUtils::LineCapToString(shapeLayer->lineCap()));
  SerializeUtils::SetFlexBufferMap(fbb, "lineJoin",
                                   SerializeUtils::LineJoinToString(shapeLayer->lineJoin()));
  SerializeUtils::SetFlexBufferMap(fbb, "miterLimit", shapeLayer->miterLimit());
  SerializeUtils::SetFlexBufferMap(fbb, "lineWidth", shapeLayer->lineWidth());
  auto lineDashPattern = shapeLayer->lineDashPattern();
  auto lineDashPatternSize = static_cast<unsigned int>(lineDashPattern.size());
  SerializeUtils::SetFlexBufferMap(fbb, "lineDashPattern", lineDashPatternSize, false,
                                   lineDashPatternSize);
  SerializeUtils::SetFlexBufferMap(fbb, "lineDashPhase", shapeLayer->lineDashPhase());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeStart", shapeLayer->strokeStart());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeEnd", shapeLayer->strokeEnd());
  SerializeUtils::SetFlexBufferMap(fbb, "lineDashAdaptive", shapeLayer->lineDashAdaptive());
  SerializeUtils::SetFlexBufferMap(fbb, "strokeAlign",
                                   SerializeUtils::StrokeAlignToString(shapeLayer->strokeAlign()));
  SerializeUtils::SetFlexBufferMap(fbb, "strokeOnTop", shapeLayer->strokeOnTop());
}
void LayerSerialization::SerializeSolidLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  SolidLayer* solidLayer = static_cast<SolidLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "width", solidLayer->width());
  SerializeUtils::SetFlexBufferMap(fbb, "height", solidLayer->height());
  SerializeUtils::SetFlexBufferMap(fbb, "radiusX", solidLayer->radiusX());
  SerializeUtils::SetFlexBufferMap(fbb, "radiusY", solidLayer->radiusY());
}
void LayerSerialization::SerializeTextLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  TextLayer* textLayer = static_cast<TextLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "text", textLayer->text());
  SerializeUtils::SetFlexBufferMap(fbb, "textColor", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "font", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "width", textLayer->width());
  SerializeUtils::SetFlexBufferMap(fbb, "height", textLayer->height());
  SerializeUtils::SetFlexBufferMap(fbb, "textAlign",
                                   SerializeUtils::TextAlignToString(textLayer->textAlign()));
  SerializeUtils::SetFlexBufferMap(fbb, "autoWrap", textLayer->autoWrap());
}
}  // namespace tgfx
#endif
