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

#include "tgfx/layers/LayerRecorder.h"
#include "layers/contents/ComposeContent.h"
#include "layers/contents/GeometryContent.h"
#include "layers/contents/PathContent.h"
#include "layers/contents/RRectContent.h"
#include "layers/contents/RectContent.h"
#include "layers/contents/ShapeContent.h"
#include "layers/contents/TextContent.h"

namespace tgfx {

LayerRecorder::LayerRecorder() = default;

LayerRecorder::~LayerRecorder() = default;

void LayerRecorder::addRect(const Rect& rect, const LayerPaint& paint) {
  if (rect.isEmpty()) {
    return;
  }
  auto& list = paint.drawOrder == DrawOrder::AboveChildren ? foregrounds : contents;
  list.push_back(std::make_unique<RectContent>(rect, paint));
}

void LayerRecorder::addRRect(const RRect& rRect, const LayerPaint& paint) {
  if (rRect.rect.isEmpty()) {
    return;
  }
  if (rRect.isRect()) {
    addRect(rRect.rect, paint);
    return;
  }
  auto& list = paint.drawOrder == DrawOrder::AboveChildren ? foregrounds : contents;
  list.push_back(std::make_unique<RRectContent>(rRect, paint));
}

void LayerRecorder::addPath(const Path& path, const LayerPaint& paint) {
  if (path.isEmpty()) {
    return;
  }
  Rect rect = {};
  if (path.isRect(&rect)) {
    addRect(rect, paint);
    return;
  }
  RRect rRect = {};
  if (path.isRRect(&rRect)) {
    addRRect(rRect, paint);
    return;
  }
  auto& list = paint.drawOrder == DrawOrder::AboveChildren ? foregrounds : contents;
  list.push_back(std::make_unique<PathContent>(path, paint));
}

void LayerRecorder::addShape(std::shared_ptr<Shape> shape, const LayerPaint& paint) {
  if (shape == nullptr) {
    return;
  }
  if (shape->isSimplePath()) {
    addPath(shape->getPath(), paint);
    return;
  }
  auto& list = paint.drawOrder == DrawOrder::AboveChildren ? foregrounds : contents;
  list.push_back(std::make_unique<ShapeContent>(std::move(shape), paint));
}

void LayerRecorder::addTextBlob(std::shared_ptr<TextBlob> textBlob, const LayerPaint& paint) {
  if (textBlob == nullptr) {
    return;
  }
  auto& list = paint.drawOrder == DrawOrder::AboveChildren ? foregrounds : contents;
  list.push_back(std::make_unique<TextContent>(std::move(textBlob), paint));
}

std::unique_ptr<LayerContent> LayerRecorder::finishRecording() {
  if (contents.empty() && foregrounds.empty()) {
    return nullptr;
  }

  if (contents.size() == 1 && foregrounds.empty()) {
    return std::move(contents[0]);
  }

  // Merge contents and foregrounds.
  auto foregroundStartIndex = contents.size();
  std::vector<std::unique_ptr<GeometryContent>> allContents = {};
  allContents.reserve(contents.size() + foregrounds.size());
  for (auto& content : contents) {
    allContents.push_back(std::move(content));
  }
  for (auto& content : foregrounds) {
    allContents.push_back(std::move(content));
  }

  // Phase 1: Group by geometry.
  std::vector<std::vector<GeometryContent*>> groups = {};
  for (const auto& content : allContents) {
    if (groups.empty() || !groups.back()[0]->hasSameGeometry(content.get())) {
      groups.push_back({content.get()});
    } else {
      groups.back().push_back(content.get());
    }
  }

  // Phase 2: Collect contours from groups.
  std::vector<GeometryContent*> contours = {};
  bool needContours = false;
  for (const auto& group : groups) {
    auto nonImageContent = std::find_if(group.begin(), group.end(), [](auto content) {
      return content->shader == nullptr || !content->shader->isAImage();
    });
    if (nonImageContent == group.end()) {
      // All image shaders, collect all.
      for (auto content : group) {
        contours.push_back(content);
      }
    } else {
      contours.push_back(*nonImageContent);
      if (group.size() > 1) {
        needContours = true;
      }
    }
  }
  if (!needContours) {
    contours.clear();
  }

  return std::make_unique<ComposeContent>(std::move(allContents), foregroundStartIndex,
                                          std::move(contours));
}

}  // namespace tgfx
