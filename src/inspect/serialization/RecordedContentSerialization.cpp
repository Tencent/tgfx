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
#include "core/utils/Types.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/PathContent.h"
#include "layers/contents/RRectContent.h"
#include "layers/contents/RectContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/TextContent.h"

namespace tgfx {

using ContentType = Types::LayerContentType;

static std::string ContentTypeToString(ContentType type) {
  switch (type) {
    case ContentType::Rect:
      return "Rect";
    case ContentType::RRect:
      return "RRect";
    case ContentType::Path:
      return "Path";
    case ContentType::Shape:
      return "Shape";
    case ContentType::Text:
      return "Text";
    case ContentType::Compose:
      return "Compose";
    default:
      return "Unknown";
  }
}

static std::string PaintStyleToString(bool hasStroke) {
  return hasStroke ? "Stroke" : "Fill";
}

static void SerializeBounds(flexbuffers::Builder& fbb, const Rect& bounds) {
  fbb.Key("bounds");
  auto boundsStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "left", bounds.left);
  SerializeUtils::SetFlexBufferMap(fbb, "top", bounds.top);
  SerializeUtils::SetFlexBufferMap(fbb, "right", bounds.right);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", bounds.bottom);
  fbb.EndMap(boundsStart);
}

static void SerializeColor(flexbuffers::Builder& fbb, const Color& color) {
  fbb.Key("color");
  auto colorStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "red", color.red);
  SerializeUtils::SetFlexBufferMap(fbb, "green", color.green);
  SerializeUtils::SetFlexBufferMap(fbb, "blue", color.blue);
  SerializeUtils::SetFlexBufferMap(fbb, "alpha", color.alpha);
  fbb.EndMap(colorStart);
}

static void SerializeStroke(flexbuffers::Builder& fbb, const Stroke& stroke) {
  fbb.Key("stroke");
  auto strokeStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "width", stroke.width);
  SerializeUtils::SetFlexBufferMap(fbb, "cap", static_cast<int>(stroke.cap));
  SerializeUtils::SetFlexBufferMap(fbb, "join", static_cast<int>(stroke.join));
  SerializeUtils::SetFlexBufferMap(fbb, "miterLimit", stroke.miterLimit);
  fbb.EndMap(strokeStart);
}

static void SerializeGeometryContent(flexbuffers::Builder& fbb, const GeometryContent* content) {
  SerializeColor(fbb, content->color);
  SerializeUtils::SetFlexBufferMap(fbb, "hasShader", content->shader != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(content->blendMode));
  SerializeUtils::SetFlexBufferMap(fbb, "style", PaintStyleToString(content->stroke != nullptr));
  if (content->stroke) {
    SerializeStroke(fbb, *content->stroke);
  }
}

static void SerializeRectContent(flexbuffers::Builder& fbb, const RectContent* content) {
  SerializeGeometryContent(fbb, content);
  fbb.Key("rect");
  auto rectStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "left", content->rect.left);
  SerializeUtils::SetFlexBufferMap(fbb, "top", content->rect.top);
  SerializeUtils::SetFlexBufferMap(fbb, "right", content->rect.right);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", content->rect.bottom);
  fbb.EndMap(rectStart);
}

static void SerializeRRectContent(flexbuffers::Builder& fbb, const RRectContent* content) {
  SerializeGeometryContent(fbb, content);
  fbb.Key("rRect");
  auto rRectStart = fbb.StartMap();
  fbb.Key("rect");
  auto rectStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "left", content->rRect.rect.left);
  SerializeUtils::SetFlexBufferMap(fbb, "top", content->rRect.rect.top);
  SerializeUtils::SetFlexBufferMap(fbb, "right", content->rRect.rect.right);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", content->rRect.rect.bottom);
  fbb.EndMap(rectStart);
  fbb.Key("radii");
  auto radiiStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "x", content->rRect.radii.x);
  SerializeUtils::SetFlexBufferMap(fbb, "y", content->rRect.radii.y);
  fbb.EndMap(radiiStart);
  fbb.EndMap(rRectStart);
}

static void SerializePathContent(flexbuffers::Builder& fbb, const PathContent* content) {
  SerializeGeometryContent(fbb, content);
  auto bounds = content->path.getBounds();
  fbb.Key("pathBounds");
  auto pathBoundsStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "left", bounds.left);
  SerializeUtils::SetFlexBufferMap(fbb, "top", bounds.top);
  SerializeUtils::SetFlexBufferMap(fbb, "right", bounds.right);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", bounds.bottom);
  fbb.EndMap(pathBoundsStart);
}

static void SerializeShapeContent(flexbuffers::Builder& fbb, const ShapeContent* content) {
  SerializeGeometryContent(fbb, content);
  if (content->shape) {
    auto bounds = content->shape->getBounds();
    fbb.Key("shapeBounds");
    auto shapeBoundsStart = fbb.StartMap();
    SerializeUtils::SetFlexBufferMap(fbb, "left", bounds.left);
    SerializeUtils::SetFlexBufferMap(fbb, "top", bounds.top);
    SerializeUtils::SetFlexBufferMap(fbb, "right", bounds.right);
    SerializeUtils::SetFlexBufferMap(fbb, "bottom", bounds.bottom);
    fbb.EndMap(shapeBoundsStart);
  }
}

static void SerializeTextContent(flexbuffers::Builder& fbb, const TextContent* content) {
  SerializeGeometryContent(fbb, content);
  if (content->textBlob) {
    auto bounds = content->textBlob->getBounds();
    fbb.Key("textBounds");
    auto textBoundsStart = fbb.StartMap();
    SerializeUtils::SetFlexBufferMap(fbb, "left", bounds.left);
    SerializeUtils::SetFlexBufferMap(fbb, "top", bounds.top);
    SerializeUtils::SetFlexBufferMap(fbb, "right", bounds.right);
    SerializeUtils::SetFlexBufferMap(fbb, "bottom", bounds.bottom);
    fbb.EndMap(textBoundsStart);
  }
}

std::shared_ptr<Data> RecordedContentSerialization::Serialize(
    const LayerContent* content, SerializeUtils::ComplexObjSerMap*,
    SerializeUtils::RenderableObjSerMap*) {
  DEBUG_ASSERT(content != nullptr)
  flexbuffers::Builder fbb;
  size_t startMap;
  size_t contentMap;
  SerializeUtils::SerializeBegin(fbb, tgfx::inspect::LayerTreeMessage::LayerSubAttribute, startMap,
                                 contentMap);

  auto type = Types::Get(content);
  SerializeUtils::SetFlexBufferMap(fbb, "type", ContentTypeToString(type));
  SerializeBounds(fbb, content->getBounds());

  switch (type) {
    case ContentType::Rect:
      SerializeRectContent(fbb, static_cast<const RectContent*>(content));
      break;
    case ContentType::RRect:
      SerializeRRectContent(fbb, static_cast<const RRectContent*>(content));
      break;
    case ContentType::Path:
      SerializePathContent(fbb, static_cast<const PathContent*>(content));
      break;
    case ContentType::Shape:
      SerializeShapeContent(fbb, static_cast<const ShapeContent*>(content));
      break;
    case ContentType::Text:
      SerializeTextContent(fbb, static_cast<const TextContent*>(content));
      break;
    case ContentType::Compose:
      // ComposeContent contains multiple contents, just show count.
      SerializeUtils::SetFlexBufferMap(fbb, "isComposed", true);
      break;
    default:
      break;
  }

  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
}  // namespace tgfx
