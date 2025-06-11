/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "WebMask.h"
#include "WebTypeface.h"
#include "core/GlyphRunList.h"
#include "core/utils/ApplyStrokeToBound.h"
#include "core/utils/Log.h"
#include "platform/web/WebImageBuffer.h"
#include "platform/web/WebImageStream.h"

using namespace emscripten;

namespace tgfx {
std::shared_ptr<Mask> Mask::Make(int width, int height, bool) {
  auto canvas = val::module_property("tgfx").call<val>("createCanvas2D", width, height);
  if (!canvas.as<bool>()) {
    return nullptr;
  }
  auto buffer = WebImageBuffer::MakeAdopted(canvas);
  auto webMaskClass = val::module_property("WebMask");
  if (!webMaskClass.as<bool>()) {
    return nullptr;
  }
  auto webMask = webMaskClass.call<val>("create", canvas);
  if (!webMask.as<bool>()) {
    return nullptr;
  }
  auto stream = WebImageStream::MakeFrom(canvas, width, height, true);
  if (stream == nullptr) {
    return nullptr;
  }
  return std::make_shared<WebMask>(std::move(buffer), std::move(stream), webMask);
}

WebMask::WebMask(std::shared_ptr<ImageBuffer> buffer, std::shared_ptr<WebImageStream> stream,
                 emscripten::val webMask)
    : buffer(std::move(buffer)), stream(std::move(stream)), webMask(webMask) {
}

void WebMask::clear() {
  aboutToFill();
  stream->markContentDirty(Rect::MakeWH(width(), height()));
  webMask.call<void>("clear");
}

static void Iterator(PathVerb verb, const Point points[4], void* info) {
  auto path2D = reinterpret_cast<val*>(info);
  switch (verb) {
    case PathVerb::Move:
      path2D->call<void>("moveTo", points[0].x, points[0].y);
      break;
    case PathVerb::Line:
      path2D->call<void>("lineTo", points[1].x, points[1].y);
      break;
    case PathVerb::Quad:
      path2D->call<void>("quadraticCurveTo", points[1].x, points[1].y, points[2].x, points[2].y);
      break;
    case PathVerb::Cubic:
      path2D->call<void>("bezierCurveTo", points[1].x, points[1].y, points[2].x, points[2].y,
                         points[3].x, points[3].y);
      break;
    case PathVerb::Close:
      path2D->call<void>("closePath");
      break;
  }
}

void WebMask::onFillPath(const Path& path, const Matrix& matrix, bool /*antiAlias*/, bool) {
  // The antiAlias parameter is ignored because the canvas2d API does not support anti-aliasing.
  if (path.isEmpty()) {
    return;
  }
  auto path2DClass = val::global("Path2D");
  if (!path2DClass.as<bool>()) {
    return;
  }
  aboutToFill();
  auto finalPath = path;
  finalPath.transform(matrix);
  stream->markContentDirty(finalPath.getBounds());
  auto path2D = path2DClass.new_();
  finalPath.decompose(Iterator, &path2D);
  webMask.call<void>("fillPath", path2D, path.getFillType());
}

static void GetTextsAndPositions(const GlyphRun* glyphRun, std::vector<std::string>* texts,
                                 std::vector<Point>* points) {
  auto typeface = std::static_pointer_cast<WebTypeface>(glyphRun->font.getTypeface());
  auto& glyphIDs = glyphRun->glyphs;
  auto& positions = glyphRun->positions;
  for (size_t i = 0; i < glyphIDs.size(); ++i) {
    texts->push_back(typeface->getText(glyphIDs[i]));
    points->push_back(positions[i]);
  }
}

bool WebMask::onFillText(const GlyphRunList* glyphRunList, const Stroke* stroke,
                         const Matrix& matrix, bool) {
  aboutToFill();
  auto bounds = glyphRunList->getBounds(matrix.getMaxScale());
  if (bounds.isEmpty()) {
    return false;
  }
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
  }
  matrix.mapRect(&bounds);
  stream->markContentDirty(bounds);
  for (auto& glyphRun : glyphRunList->glyphRuns()) {
    std::vector<std::string> texts = {};
    std::vector<Point> points = {};
    auto typeface = glyphRun.font.getTypeface();
    if (!typeface || typeface->uniqueID() != typeface->getActiveID()) {
      return false;
    }
    GetTextsAndPositions(&glyphRun, &texts, &points);
    auto webFont = val::object();
    webFont.set("name", typeface->fontFamily());
    webFont.set("style", typeface->fontStyle());
    webFont.set("size", glyphRun.font.getSize());
    webFont.set("bold", glyphRun.font.isFauxBold());
    webFont.set("italic", glyphRun.font.isFauxItalic());
    if (stroke) {
      webMask.call<void>("strokeText", webFont, *stroke, texts, points, matrix);
    } else {
      webMask.call<void>("fillText", webFont, texts, points, matrix);
    }
  }
  return true;
}

void WebMask::aboutToFill() {
  if (buffer.use_count() == 1) {
    return;
  }
  auto canvas = val::module_property("tgfx").call<val>("createCanvas2D", width(), height());
  if (!canvas.as<bool>()) {
    ABORT("WebMask::aboutToFill() : Failed to create new Canvas2D!");
    return;
  }
  buffer = WebImageBuffer::MakeAdopted(canvas);
  webMask.call<void>("updateCanvas", canvas);
  stream->source = canvas;
}
}  // namespace tgfx
