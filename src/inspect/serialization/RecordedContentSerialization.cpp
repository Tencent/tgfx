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
#include <string>
#include "core/utils/Types.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/MatrixContent.h"
#include "layers/contents/PathContent.h"
#include "layers/contents/RRectContent.h"
#include "layers/contents/RRectsContent.h"
#include "layers/contents/RectContent.h"
#include "layers/contents/RectsContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/StrokeContent.h"
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
    case ContentType::Rects:
      return "Rects";
    case ContentType::RRects:
      return "RRects";
    case ContentType::Matrix:
      return "Matrix";
    case ContentType::Stroke:
      return "Stroke";
    default:
      return "Unknown";
  }
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

static void SerializeDrawContent(flexbuffers::Builder& fbb, const DrawContent* content) {
  SerializeColor(fbb, content->getColor());
  SerializeUtils::SetFlexBufferMap(fbb, "hasShader", content->getShader() != nullptr);
  SerializeUtils::SetFlexBufferMap(fbb, "blendMode",
                                   SerializeUtils::BlendModeToString(content->getBlendMode()));
}

static void SerializeRect(flexbuffers::Builder& fbb, const char* key, const Rect& rect) {
  fbb.Key(key);
  auto rectStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "left", rect.left);
  SerializeUtils::SetFlexBufferMap(fbb, "top", rect.top);
  SerializeUtils::SetFlexBufferMap(fbb, "right", rect.right);
  SerializeUtils::SetFlexBufferMap(fbb, "bottom", rect.bottom);
  fbb.EndMap(rectStart);
}

static void SerializeRectContent(flexbuffers::Builder& fbb, const RectContent* content) {
  SerializeDrawContent(fbb, content);
  SerializeRect(fbb, "rect", content->rect);
}

static void SerializeRectsContent(flexbuffers::Builder& fbb, const RectsContent* content) {
  SerializeDrawContent(fbb, content);
  SerializeUtils::SetFlexBufferMap(fbb, "rectsCount", static_cast<int>(content->rects.size()));
}

static void SerializeRRectContent(flexbuffers::Builder& fbb, const RRectContent* content) {
  SerializeDrawContent(fbb, content);
  fbb.Key("rRect");
  auto rRectStart = fbb.StartMap();
  SerializeRect(fbb, "rect", content->rRect.rect);
  fbb.Key("radii");
  auto radiiStart = fbb.StartMap();
  SerializeUtils::SetFlexBufferMap(fbb, "x", content->rRect.radii.x);
  SerializeUtils::SetFlexBufferMap(fbb, "y", content->rRect.radii.y);
  fbb.EndMap(radiiStart);
  fbb.EndMap(rRectStart);
}

static void SerializeRRectsContent(flexbuffers::Builder& fbb, const RRectsContent* content) {
  SerializeDrawContent(fbb, content);
  SerializeUtils::SetFlexBufferMap(fbb, "rRectsCount", static_cast<int>(content->rRects.size()));
}

static void SerializePathContent(flexbuffers::Builder& fbb, const PathContent* content) {
  SerializeDrawContent(fbb, content);
  SerializeRect(fbb, "pathBounds", content->path.getBounds());
}

static void SerializeShapeContent(flexbuffers::Builder& fbb, const ShapeContent* content) {
  SerializeDrawContent(fbb, content);
  if (content->shape) {
    SerializeRect(fbb, "shapeBounds", content->shape->getBounds());
  }
}

static void SerializeTextContent(flexbuffers::Builder& fbb, const TextContent* content) {
  SerializeDrawContent(fbb, content);
  if (content->textBlob) {
    SerializeRect(fbb, "textBounds", content->textBlob->getBounds());
  }
}

static void SerializeMatrixContent(flexbuffers::Builder& fbb, const MatrixContent* content) {
  fbb.Key("matrix");
  auto matrixStart = fbb.StartMap();
  float buffer[9] = {};
  content->_matrix.get9(buffer);
  for (int i = 0; i < 9; i++) {
    auto key = "[" + std::to_string(i) + "]";
    SerializeUtils::SetFlexBufferMap(fbb, key.c_str(), buffer[i]);
  }
  fbb.EndMap(matrixStart);
}

  static void SerializeStrokeContent(flexbuffers::Builder& fbb, const StrokeContent* content) {
    SerializeStroke(fbb, content->_stroke);
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
    case ContentType::Rects:
      SerializeRectsContent(fbb, static_cast<const RectsContent*>(content));
      break;
    case ContentType::RRect:
      SerializeRRectContent(fbb, static_cast<const RRectContent*>(content));
      break;
    case ContentType::RRects:
      SerializeRRectsContent(fbb, static_cast<const RRectsContent*>(content));
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
    case ContentType::Matrix:
      SerializeMatrixContent(fbb, static_cast<const MatrixContent*>(content));
      break;
    case ContentType::Stroke:
      SerializeStrokeContent(fbb, static_cast<const StrokeContent*>(content));
      break;
    default:
      break;
  }

  SerializeUtils::SerializeEnd(fbb, startMap, contentMap);
  return Data::MakeWithCopy(fbb.GetBuffer().data(), fbb.GetBuffer().size());
}
}  // namespace tgfx
