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
#ifdef TGFX_ENABLE_PROFILING

#include "LayerContentSerialization.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/ImageContent.h"
#include "layers/contents/RasterizedContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/SolidContent.h"
#include "layers/contents/TextContent.h"

namespace tgfx {

std::shared_ptr<Data> LayerContentSerialization::serializeLayerContent(LayerContent* layerContent) {
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::serializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = layerContent->Type();
  switch (type) {
    case LayerContentType::LayerContent:
      serializeBasicLayerContentImpl(fbb, layerContent);
      break;
    case LayerContentType::ComposeContent:
      serializeComposeContentImpl(fbb, layerContent);
      break;
    case LayerContentType::ImageContent:
      serializeImageContentImpl(fbb, layerContent);
      break;
    case LayerContentType::RasterizedContent:
      serializeRasterizedContentImpl(fbb, layerContent);
      break;
    case LayerContentType::ShapeContent:
      serializeShapeContentImpl(fbb, layerContent);
      break;
    case LayerContentType::SolidContent:
      serializeSolidContentImpl(fbb, layerContent);
      break;
    case LayerContentType::TextContent:
      serializeTextContentImpl(fbb, layerContent);
      break;
  }
  SerializeUtils::serializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
void LayerContentSerialization::serializeBasicLayerContentImpl(flexbuffers::Builder& fbb,
                                                               LayerContent* layerContent) {
  (void)fbb;
  (void)layerContent;
}
void LayerContentSerialization::serializeComposeContentImpl(flexbuffers::Builder& fbb,
                                                            LayerContent* layerContent) {
  serializeBasicLayerContentImpl(fbb, layerContent);
  ComposeContent* composeContent = static_cast<ComposeContent*>(layerContent);
  auto contentsSize = static_cast<unsigned int>(composeContent->contents.size());
  SerializeUtils::setFlexBufferMap(fbb, "Contents", contentsSize, false, contentsSize);
}
void LayerContentSerialization::serializeImageContentImpl(flexbuffers::Builder& fbb,
                                                          LayerContent* layerContent) {
  serializeBasicLayerContentImpl(fbb, layerContent);
  ImageContent* imageContent = static_cast<ImageContent*>(layerContent);
  SerializeUtils::setFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(imageContent->image.get()), true,
                                   imageContent->image != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Sampling", "", false, true);
}
void LayerContentSerialization::serializeRasterizedContentImpl(flexbuffers::Builder& fbb,
                                                               LayerContent* layerContent) {
  serializeBasicLayerContentImpl(fbb, layerContent);
  RasterizedContent* rasterizedContent = static_cast<RasterizedContent*>(layerContent);
  SerializeUtils::setFlexBufferMap(fbb, "ContextID", rasterizedContent->_contextID);
  SerializeUtils::setFlexBufferMap(fbb, "Image",
                                   reinterpret_cast<uint64_t>(rasterizedContent->image.get()), true,
                                   rasterizedContent->image != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "Matrix", "", false, true);
}
void LayerContentSerialization::serializeShapeContentImpl(flexbuffers::Builder& fbb,
                                                          LayerContent* layerContent) {
  serializeBasicLayerContentImpl(fbb, layerContent);
  ShapeContent* shapeContent = static_cast<ShapeContent*>(layerContent);
  SerializeUtils::setFlexBufferMap(fbb, "Bounds", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "FillShape",
                                   reinterpret_cast<uint64_t>(shapeContent->fillShape.get()), true,
                                   shapeContent->fillShape != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "StrokeShape",
                                   reinterpret_cast<uint64_t>(shapeContent->strokeShape.get()),
                                   true, shapeContent->strokeShape != nullptr);
  auto paintListSize = static_cast<unsigned int>(shapeContent->paintList.size());
  SerializeUtils::setFlexBufferMap(fbb, "PaintList", paintListSize, false, paintListSize);
  SerializeUtils::setFlexBufferMap(fbb, "FillPaintCount", shapeContent->fillPaintCount);
}
void LayerContentSerialization::serializeSolidContentImpl(flexbuffers::Builder& fbb,
                                                          LayerContent* layerContent) {
  serializeBasicLayerContentImpl(fbb, layerContent);
  //SolidContent* solidContent = static_cast<SolidContent*>(layerContent);
  SerializeUtils::setFlexBufferMap(fbb, "RRect", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "Color", "", false, true);
}
void LayerContentSerialization::serializeTextContentImpl(flexbuffers::Builder& fbb,
                                                         LayerContent* layerContent) {
  serializeBasicLayerContentImpl(fbb, layerContent);
  TextContent* textContent = static_cast<TextContent*>(layerContent);
  SerializeUtils::setFlexBufferMap(fbb, "Bounds", "", false, true);
  SerializeUtils::setFlexBufferMap(fbb, "TextBlob",
                                   reinterpret_cast<uint64_t>(textContent->textBlob.get()), true,
                                   textContent->textBlob != nullptr);
  SerializeUtils::setFlexBufferMap(fbb, "TextColor", "", false, true);
}
}  // namespace tgfx
#endif
