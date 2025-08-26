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

#include "RecordedContentSerialization.h"
#include "layers/contents/ContourContent.h"
#include "layers/contents/DefaultContent.h"
#include "layers/contents/ForegroundContent.h"

namespace tgfx {

static void SerializeDefaultContentImpl(flexbuffers::Builder& fbb, const LayerContent* content,
                                        SerializeUtils::ComplexObjSerMap* map,
                                        SerializeUtils::RenderableObjSerMap* rosMap) {
  auto defaultContent = static_cast<const DefaultContent*>(content);
  auto picture = defaultContent->content;
  auto pictureID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "content", reinterpret_cast<uint64_t>(picture.get()), true,
                                   picture != nullptr, pictureID, true);
  SerializeUtils::FillRenderableObjSerMap(picture, pictureID, rosMap);
  SerializeUtils::FillComplexObjSerMap(picture, pictureID, map);
}

static void SerializeForegroundContentImpl(flexbuffers::Builder& fbb, const LayerContent* content,
                                           SerializeUtils::ComplexObjSerMap* map,
                                           SerializeUtils::RenderableObjSerMap* rosMap) {
  auto foregroundContent = static_cast<const ForegroundContent*>(content);
  auto foreground = foregroundContent->foreground;
  auto background = foregroundContent->background;
  auto foregroundID = SerializeUtils::GetObjID();
  auto backgroundID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "foreground", reinterpret_cast<uint64_t>(foreground.get()),
                                   true, foreground != nullptr, foregroundID, true);
  SerializeUtils::FillRenderableObjSerMap(foreground, foregroundID, rosMap);
  SerializeUtils::FillComplexObjSerMap(foreground, foregroundID, map);
  SerializeUtils::SetFlexBufferMap(fbb, "background", reinterpret_cast<uint64_t>(background.get()),
                                   true, background != nullptr, backgroundID, true);
  SerializeUtils::FillRenderableObjSerMap(background, backgroundID, rosMap);
  SerializeUtils::FillComplexObjSerMap(background, backgroundID, map);
}

static void SerializeContourContentImpl(flexbuffers::Builder& fbb, const LayerContent* content,
                                        SerializeUtils::ComplexObjSerMap* map,
                                        SerializeUtils::RenderableObjSerMap* rosMap) {
  auto contourContent = static_cast<const ContourContent*>(content);
  auto contour = contourContent->contour;
  auto contourID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "contour", reinterpret_cast<uint64_t>(contour.get()), true,
                                   contour != nullptr, contourID, true);
  SerializeUtils::FillRenderableObjSerMap(contour, contourID, rosMap);
  SerializeUtils::FillComplexObjSerMap(contour, contourID, map);
  auto contentID = SerializeUtils::GetObjID();
  SerializeUtils::SetFlexBufferMap(fbb, "recordedContent", "", false, (bool)contourContent->content,
                                   contentID);
  SerializeUtils::FillComplexObjSermap(contourContent->content.get(), contentID, map, rosMap);
}

std::shared_ptr<Data> RecordedContentSerialization::Serialize(
    const LayerContent* content, SerializeUtils::ComplexObjSerMap* map,
    SerializeUtils::RenderableObjSerMap* rosMap) {
  DEBUG_ASSERT(content != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute, startMap,
                                 contentMap);
  auto type = Types::Get(content);
  SerializeUtils::SetFlexBufferMap(fbb, "type", SerializeUtils::RecordedContentTypeToString(type));
  switch (type) {
    case Types::LayerContentType::Default:
      SerializeDefaultContentImpl(fbb, content, map, rosMap);
      break;
    case Types::LayerContentType::Foreground:
      SerializeForegroundContentImpl(fbb, content, map, rosMap);
      break;
    case Types::LayerContentType::Contour:
      SerializeContourContentImpl(fbb, content, map, rosMap);
      break;
    default:
      DEBUG_ASSERT(false);
  }
  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
}  // namespace tgfx