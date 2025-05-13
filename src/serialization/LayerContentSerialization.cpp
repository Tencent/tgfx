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

#include "LayerContentSerialization.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/ImageContent.h"
#include "layers/contents/RasterizedContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/SolidContent.h"
#include "layers/contents/TextContent.h"

namespace tgfx {

std::shared_ptr<Data> LayerContentSerialization::Serialize(LayerContent* layerContent) {
  DEBUG_ASSERT(layerContent != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, "LayerAttribute", startMap, contentMap);
  auto type = Types::Get(layerContent);
  switch (type) {
    case Types::LayerContentType::LayerContent:
      SerializeBasicLayerContentImpl(fbb, layerContent);
      break;
    case Types::LayerContentType::ComposeContent:
      SerializeComposeContentImpl(fbb, layerContent);
      break;
    case Types::LayerContentType::ImageContent:
      SerializeImageContentImpl(fbb, layerContent);
      break;
    case Types::LayerContentType::RasterizedContent:
      SerializeRasterizedContentImpl(fbb, layerContent);
      break;
    case Types::LayerContentType::ShapeContent:
      SerializeShapeContentImpl(fbb, layerContent);
      break;
    case Types::LayerContentType::SolidContent:
      SerializeSolidContentImpl(fbb, layerContent);
      break;
    case Types::LayerContentType::TextContent:
      SerializeTextContentImpl(fbb, layerContent);
      break;
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}

void LayerContentSerialization::SerializeBasicLayerContentImpl(flexbuffers::Builder& fbb,
                                                               LayerContent* layerContent) {
  (void)fbb;
  (void)layerContent;
}

void LayerContentSerialization::SerializeComposeContentImpl(flexbuffers::Builder& fbb,
                                                            LayerContent* layerContent) {
  SerializeBasicLayerContentImpl(fbb, layerContent);
  ComposeContent* composeContent = static_cast<ComposeContent*>(layerContent);
  auto contentsSize = static_cast<unsigned int>(composeContent->contents.size());
  SerializeUtils::SetFlexBufferMap(fbb, "contents", contentsSize, false, contentsSize);
}

void LayerContentSerialization::SerializeImageContentImpl(flexbuffers::Builder& fbb,
                                                          LayerContent* layerContent) {
  SerializeBasicLayerContentImpl(fbb, layerContent);
  ImageContent* imageContent = static_cast<ImageContent*>(layerContent);
  SerializeUtils::SetFlexBufferMap(fbb, "image",
                                   reinterpret_cast<uint64_t>(imageContent->image.get()), true,
                                   imageContent->image != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "sampling", "", false, true);
}

void LayerContentSerialization::SerializeRasterizedContentImpl(flexbuffers::Builder& fbb,
                                                               LayerContent* layerContent) {
  SerializeBasicLayerContentImpl(fbb, layerContent);
  RasterizedContent* rasterizedContent = static_cast<RasterizedContent*>(layerContent);
  SerializeUtils::SetFlexBufferMap(fbb, "contextID", rasterizedContent->contextID());
  auto image = rasterizedContent->getImage();
  SerializeUtils::SetFlexBufferMap(fbb, "image", reinterpret_cast<uint64_t>(image.get()), true,
                                   image != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "matrix", "", false, true);
}

void LayerContentSerialization::SerializeShapeContentImpl(flexbuffers::Builder& fbb,
                                                          LayerContent* layerContent) {
  SerializeBasicLayerContentImpl(fbb, layerContent);
  ShapeContent* shapeContent = static_cast<ShapeContent*>(layerContent);
  SerializeUtils::SetFlexBufferMap(fbb, "bounds", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "fillShape",
                                   reinterpret_cast<uint64_t>(shapeContent->fillShape.get()), true,
                                   shapeContent->fillShape != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "strokeShape",
                                   reinterpret_cast<uint64_t>(shapeContent->strokeShape.get()),
                                   true, shapeContent->strokeShape != nullptr);
  auto paintListSize = static_cast<unsigned int>(shapeContent->paintList.size());
  SerializeUtils::SetFlexBufferMap(fbb, "paintList", paintListSize, false, paintListSize);
  SerializeUtils::SetFlexBufferMap(fbb, "fillPaintCount", shapeContent->fillPaintCount);
}

void LayerContentSerialization::SerializeSolidContentImpl(flexbuffers::Builder& fbb,
                                                          LayerContent* layerContent) {
  SerializeBasicLayerContentImpl(fbb, layerContent);
  //SolidContent* solidContent = static_cast<SolidContent*>(layerContent);
  SerializeUtils::SetFlexBufferMap(fbb, "rRect", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "color", "", false, true);
}

void LayerContentSerialization::SerializeTextContentImpl(flexbuffers::Builder& fbb,
                                                         LayerContent* layerContent) {
  SerializeBasicLayerContentImpl(fbb, layerContent);
  TextContent* textContent = static_cast<TextContent*>(layerContent);
  SerializeUtils::SetFlexBufferMap(fbb, "bounds", "", false, true);
  SerializeUtils::SetFlexBufferMap(fbb, "textBlob",
                                   reinterpret_cast<uint64_t>(textContent->textBlob.get()), true,
                                   textContent->textBlob != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "textColor", "", false, true);
}
}  // namespace tgfx
#endif
