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
  SerializeUtils::SetFlexBufferMap(fbb, "Type", SerializeUtils::LayerTypeToString(layer->type()));
  SerializeUtils::SetFlexBufferMap(fbb, "Visible", layer->visible());
  SerializeUtils::SetFlexBufferMap(fbb, "ShouldRasterize", layer->shouldRasterize());
  SerializeUtils::SetFlexBufferMap(fbb, "AllowsEdgeAntialiasing", layer->allowsEdgeAntialiasing());
  SerializeUtils::SetFlexBufferMap(fbb, "AllowsGroupOpacity", layer->allowsGroupOpacity());
  SerializeUtils::SetFlexBufferMap(fbb, "ExcludeChildEffectsInLayerStyle",
                                   layer->excludeChildEffectsInLayerStyle());
  SerializeUtils::SetFlexBufferMap(fbb, "BlendMode",
                                   SerializeUtils::BlendModeToString(layer->blendMode()));
  SerializeUtils::SetFlexBufferMap(fbb, "Name", layer->name());
  SerializeUtils::SetFlexBufferMap(fbb, "Alpha", layer->alpha());
  SerializeUtils::SetFlexBufferMap(fbb, "Matrix", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "Position", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "RasterizationScale", layer->rasterizationScale());
  auto filters = layer->filters();
  auto filterSize = static_cast<unsigned int>(filters.size());
  SerializeUtils::SetFlexBufferMap(fbb, "Filters", filterSize, false, filterSize);
  auto mask = layer->mask();
  SerializeUtils::SetFlexBufferMap(fbb, "Mask", reinterpret_cast<uint64_t>(mask.get()), true,
                                   mask != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "ScrollRect", "", false, true);
  auto root = layer->root();
  SerializeUtils::SetFlexBufferMap(fbb, "Root", reinterpret_cast<uint64_t>(root), true,
                                   root != nullptr);
  auto parent = layer->parent();
  SerializeUtils::SetFlexBufferMap(fbb, "Parent", reinterpret_cast<uint64_t>(parent.get()), true,
                                   parent != nullptr);
  auto children = layer->children();
  auto childrenSize = static_cast<unsigned int>(children.size());
  SerializeUtils::SetFlexBufferMap(fbb, "Children", childrenSize, false, childrenSize);
  auto layerStyles = layer->layerStyles();
  auto layerStylesSize = static_cast<unsigned int>(layerStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "LayerStyles", layerStylesSize, false, layerStylesSize);
}
void LayerSerialization::SerializeImageLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  ImageLayer* imageLayer = static_cast<ImageLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "Sampling", "", false, true);
  auto image = imageLayer->image();
  SerializeUtils::SetFlexBufferMap(fbb, "Image", reinterpret_cast<uint64_t>(image.get()), true,
                                   image != nullptr);
}
void LayerSerialization::SerializeShapeLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  ShapeLayer* shapeLayer = static_cast<ShapeLayer*>(layer);
  auto shape = shapeLayer->shape();
  SerializeUtils::SetFlexBufferMap(fbb, "Shape", reinterpret_cast<uint64_t>(shape.get()), true,
                                   shape != nullptr);
  auto fillStyles = shapeLayer->fillStyles();
  auto fillStylesSize = static_cast<unsigned int>(fillStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "FillStyles", fillStylesSize, false, fillStylesSize);
  auto strokeStyles = shapeLayer->strokeStyles();
  auto strokeStylesSize = static_cast<unsigned int>(strokeStyles.size());
  SerializeUtils::SetFlexBufferMap(fbb, "StrokeStyles", strokeStylesSize, false, strokeStylesSize);
  SerializeUtils::SetFlexBufferMap(fbb, "LineCap",
                                   SerializeUtils::LineCapToString(shapeLayer->lineCap()));
  SerializeUtils::SetFlexBufferMap(fbb, "LineJoin",
                                   SerializeUtils::LineJoinToString(shapeLayer->lineJoin()));
  SerializeUtils::SetFlexBufferMap(fbb, "MiterLimit", shapeLayer->miterLimit());
  SerializeUtils::SetFlexBufferMap(fbb, "LineWidth", shapeLayer->lineWidth());
  auto lineDashPattern = shapeLayer->lineDashPattern();
  auto lineDashPatternSize = static_cast<unsigned int>(lineDashPattern.size());
  SerializeUtils::SetFlexBufferMap(fbb, "LineDashPattern", lineDashPatternSize, false,
                                   lineDashPatternSize);
  SerializeUtils::SetFlexBufferMap(fbb, "LineDashPhase", shapeLayer->lineDashPhase());
  SerializeUtils::SetFlexBufferMap(fbb, "StrokeStart", shapeLayer->strokeStart());
  SerializeUtils::SetFlexBufferMap(fbb, "StrokeEnd", shapeLayer->strokeEnd());
  SerializeUtils::SetFlexBufferMap(fbb, "LineDashAdaptive", shapeLayer->lineDashAdaptive());
  SerializeUtils::SetFlexBufferMap(fbb, "StrokeAlign",
                                   SerializeUtils::StrokeAlignToString(shapeLayer->strokeAlign()));
  SerializeUtils::SetFlexBufferMap(fbb, "StrokeOnTop", shapeLayer->strokeOnTop());
}
void LayerSerialization::SerializeSolidLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  SolidLayer* solidLayer = static_cast<SolidLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "Width", solidLayer->width());
  SerializeUtils::SetFlexBufferMap(fbb, "Height", solidLayer->height());
  SerializeUtils::SetFlexBufferMap(fbb, "RadiusX", solidLayer->radiusX());
  SerializeUtils::SetFlexBufferMap(fbb, "RadiusY", solidLayer->radiusY());
}
void LayerSerialization::SerializeTextLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeBasicLayerImpl(fbb, layer);
  TextLayer* textLayer = static_cast<TextLayer*>(layer);
  SerializeUtils::SetFlexBufferMap(fbb, "Text", textLayer->text());
  SerializeUtils::SetFlexBufferMap(fbb, "TextColor", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "Font", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "Width", textLayer->width());
  SerializeUtils::SetFlexBufferMap(fbb, "Height", textLayer->height());
  SerializeUtils::SetFlexBufferMap(fbb, "TextAlign",
                                   SerializeUtils::TextAlignToString(textLayer->textAlign()));
  SerializeUtils::SetFlexBufferMap(fbb, "AutoWrap", textLayer->autoWrap());
}
}  // namespace tgfx
#endif
