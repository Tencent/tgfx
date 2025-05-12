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

std::shared_ptr<Data> LayerSerialization::serializeLayer(Layer* layer) {
  DEBUG_ASSERT(layer != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layer->type();
  switch (type) {
    case LayerType::Image:
      serializeImageLayerImpl(fbb, layer);
      break;
    case LayerType::Shape:
      serializeShapeLayerImpl(fbb, layer);
      break;
    case LayerType::Text:
      serializeTextLayerImpl(fbb, layer);
      break;
    case LayerType::Solid:
      serializeSolidLayerImpl(fbb, layer);
      break;
    case LayerType::Layer:
    case LayerType::Gradient:
      serializeBasicLayerImpl(fbb, layer);
      break;
    default:
      LOGE("Unknown layer type!");
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

std::shared_ptr<Data> LayerSerialization::serializeTreeNode(
    std::shared_ptr<Layer> layer,
    std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap) {
  flexbuffers::Builder fbb;
  size_t startMap = fbb.StartMap();
  fbb.Key("Type");
  fbb.String("LayerTree");
  fbb.Key("Content");
  serializeTreeNodeImpl(fbb, layer, layerMap);
  fbb.EndMap(startMap);
  fbb.Finish();
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void LayerSerialization::serializeTreeNodeImpl(
    flexbuffers::Builder& fbb, std::shared_ptr<Layer> layer,
    std::unordered_map<uint64_t, std::shared_ptr<tgfx::Layer>>& layerMap) {
  auto startMap = fbb.StartMap();
  fbb.Key("LayerType");
  fbb.String(SerializeUtils::layerTypeToString(layer->type()));
  fbb.Key("Address");
  fbb.UInt(reinterpret_cast<uint64_t>(layer.get()));
  fbb.Key("Children");
  auto startVector = fbb.StartVector();
  std::vector<std::shared_ptr<Layer>> children = layer->children();
  for (const auto& child : children) {
    serializeTreeNodeImpl(fbb, child, layerMap);
  }
  fbb.EndVector(startVector, false, false);
  fbb.EndMap(startMap);
  layerMap.insert({reinterpret_cast<uint64_t>(layer.get()), layer});
}

void LayerSerialization::serializeBasicLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  SerializeUtils::setFlexBufferMap(fbb, "Type", "Layer");
  SerializeUtils::setFlexBufferMap(fbb, "DirtyContent", layer->bitFields.dirtyContent);
  SerializeUtils::setFlexBufferMap(fbb, "DirtyDescendents", layer->bitFields.dirtyDescendents);
  SerializeUtils::setFlexBufferMap(fbb, "DirtyTransform", layer->bitFields.dirtyTransform);
  SerializeUtils::setFlexBufferMap(fbb, "DirtyBackground", layer->bitFields.dirtyBackground);
  SerializeUtils::setFlexBufferMap(fbb, "Visible", layer->bitFields.visible);
  SerializeUtils::setFlexBufferMap(fbb, "ShouldRasterize", layer->bitFields.shouldRasterize);
  SerializeUtils::setFlexBufferMap(fbb, "AllowsEdgeAntialiasing",
                                   layer->bitFields.allowsEdgeAntialiasing);
  SerializeUtils::setFlexBufferMap(fbb, "AllowsGroupOpacity", layer->bitFields.allowsGroupOpacity);
  SerializeUtils::setFlexBufferMap(fbb, "ExcludeChildEffectsInLayerStyle",
                                   layer->bitFields.excludeChildEffectsInLayerStyle);
  SerializeUtils::setFlexBufferMap(
      fbb, "BlendMode",
      SerializeUtils::blendModeToString(static_cast<BlendMode>(layer->bitFields.blendMode)));
  SerializeUtils::setFlexBufferMap(fbb, "Name", layer->_name);
  SerializeUtils::setFlexBufferMap(fbb, "Alpha", layer->_alpha);
  SerializeUtils::setFlexBufferMap(fbb, "Matrix", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "RasterizationScale", layer->_rasterizationScale);
  auto filterSize = static_cast<unsigned int>(layer->_filters.size());
  SerializeUtils::setFlexBufferMap(fbb, "Filters", filterSize, false, filterSize);
  SerializeUtils::setFlexBufferMap(fbb, "Mask", reinterpret_cast<uint64_t>(layer->_mask.get()),
                                   true, layer->_mask != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "MaskOwner", reinterpret_cast<uint64_t>(layer->maskOwner),
                                   true, layer->maskOwner != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "ScrollRect",
                                   reinterpret_cast<uint64_t>(layer->_scrollRect.get()), true,
                                   layer->_scrollRect != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Root", reinterpret_cast<uint64_t>(layer->_root), true,
                                   layer->_root != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Parent", reinterpret_cast<uint64_t>(layer->_parent), true,
                                   layer->_parent != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "LayerContent",
                                   reinterpret_cast<uint64_t>(layer->layerContent.get()), true,
                                   layer->layerContent != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "RasterizedContent",
                                   reinterpret_cast<uint64_t>(layer->rasterizedContent.get()), true,
                                   layer->rasterizedContent != nullptr);
  auto childrenSize = static_cast<unsigned int>(layer->_children.size());
  SerializeUtils::setFlexBufferMap(fbb, "Children", childrenSize, false, childrenSize);
  auto layerStylesSize = static_cast<unsigned int>(layer->_layerStyles.size());
  SerializeUtils::setFlexBufferMap(fbb, "LayerStyles", layerStylesSize, false, layerStylesSize);
}
void LayerSerialization::serializeImageLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  serializeBasicLayerImpl(fbb, layer);
  ImageLayer* imageLayer = static_cast<ImageLayer*>(layer);
  SerializeUtils::setFlexBufferMap(fbb, "Sampling", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(imageLayer->_image.get()), true,
                                   imageLayer->_image != nullptr);
}
void LayerSerialization::serializeShapeLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  serializeBasicLayerImpl(fbb, layer);
  ShapeLayer* shapeLayer = static_cast<ShapeLayer*>(layer);
  SerializeUtils::setFlexBufferMap(fbb, "Shape",
                                   reinterpret_cast<uint64_t>(shapeLayer->_shape.get()), true,
                                   shapeLayer->_shape != nullptr);
  auto fillStylesSize = static_cast<unsigned int>(shapeLayer->_fillStyles.size());
  SerializeUtils::setFlexBufferMap(fbb, "FillStyles", fillStylesSize, false, fillStylesSize);
  auto strokeStylesSize = static_cast<unsigned int>(shapeLayer->_strokeStyles.size());
  SerializeUtils::setFlexBufferMap(fbb, "StrokeStyles", strokeStylesSize, false, strokeStylesSize);
  SerializeUtils::setFlexBufferMap(fbb, "Stroke", "", false, true);
  auto lineDashPatternSize = static_cast<unsigned int>(shapeLayer->_lineDashPattern.size());
  SerializeUtils::setFlexBufferMap(fbb, "LineDashPattern", lineDashPatternSize, false,
                                   lineDashPatternSize);
  SerializeUtils::setFlexBufferMap(fbb, "LineDashPhase", shapeLayer->_lineDashPhase);
  SerializeUtils::setFlexBufferMap(fbb, "StrokeStart", shapeLayer->_strokeStart);
  SerializeUtils::setFlexBufferMap(fbb, "StrokeEnd", shapeLayer->_strokeEnd);
}
void LayerSerialization::serializeSolidLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  serializeBasicLayerImpl(fbb, layer);
  SolidLayer* solidLayer = static_cast<SolidLayer*>(layer);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Width", solidLayer->_width);
  SerializeUtils::setFlexBufferMap(fbb, "Height", solidLayer->_height);
  SerializeUtils::setFlexBufferMap(fbb, "RadiusX", solidLayer->_radiusX);
  SerializeUtils::setFlexBufferMap(fbb, "RadiusY", solidLayer->_radiusY);
}
void LayerSerialization::serializeTextLayerImpl(flexbuffers::Builder& fbb, Layer* layer) {
  serializeBasicLayerImpl(fbb, layer);
  TextLayer* textLayer = static_cast<TextLayer*>(layer);
  SerializeUtils::setFlexBufferMap(fbb, "Text", textLayer->_text);
  SerializeUtils::setFlexBufferMap(fbb, "TextColor", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Font", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Width", textLayer->_width);
  SerializeUtils::setFlexBufferMap(fbb, "Height", textLayer->_height);
  SerializeUtils::setFlexBufferMap(fbb, "TextAlign",
                                   SerializeUtils::textAlignToString(textLayer->_textAlign));
  SerializeUtils::setFlexBufferMap(fbb, "AutoWrap", textLayer->_autoWrap);
}
}  // namespace tgfx
#endif
