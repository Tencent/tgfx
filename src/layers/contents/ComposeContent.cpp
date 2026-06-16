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

#include "ComposeContent.h"

namespace tgfx {

ComposeContent::ComposeContent(std::vector<std::unique_ptr<GeometryContent>> contents,
                               size_t foregroundStartIndex, std::vector<GeometryContent*> contours)
    : contents(std::move(contents)), foregroundStartIndex(foregroundStartIndex),
      contours(std::move(contours)) {
}

Rect ComposeContent::getBounds() const {
  auto result = Rect::MakeEmpty();
  if (!contours.empty()) {
    for (const auto& content : contours) {
      result.join(content->getBounds());
    }
  } else {
    for (const auto& content : contents) {
      result.join(content->getBounds());
    }
  }
  return result;
}

Rect ComposeContent::getTightBounds(const Matrix& matrix) const {
  auto result = Rect::MakeEmpty();
  if (!contours.empty()) {
    for (const auto& content : contours) {
      result.join(content->getTightBounds(matrix));
    }
  } else {
    for (const auto& content : contents) {
      result.join(content->getTightBounds(matrix));
    }
  }
  return result;
}

bool ComposeContent::hitTestPoint(float localX, float localY) const {
  for (const auto& content : contents) {
    if (content->hitTestPoint(localX, localY)) {
      return true;
    }
  }
  return false;
}

void ComposeContent::drawContour(Canvas* canvas, bool antiAlias) const {
  if (!contours.empty()) {
    for (const auto geometry : contours) {
      geometry->drawContour(canvas, antiAlias);
    }
  } else {
    for (const auto& content : contents) {
      content->drawContour(canvas, antiAlias);
    }
  }
}

bool ComposeContent::contourEqualsOpaqueContent() const {
  for (const auto& content : contents) {
    if (!content->contourEqualsOpaqueContent()) {
      return false;
    }
  }
  return true;
}

bool ComposeContent::hasBlendMode() const {
  for (const auto& content : contents) {
    if (content->hasBlendMode()) {
      return true;
    }
  }
  return false;
}

bool ComposeContent::drawDefault(Canvas* canvas, float alpha, bool antiAlias) const {
  for (size_t i = 0; i < foregroundStartIndex; ++i) {
    contents[i]->drawDefault(canvas, alpha, antiAlias);
  }
  return foregroundStartIndex < contents.size();
}

void ComposeContent::drawForeground(Canvas* canvas, float alpha, bool antiAlias) const {
  for (size_t i = foregroundStartIndex; i < contents.size(); ++i) {
    contents[i]->drawDefault(canvas, alpha, antiAlias);
  }
}

}  // namespace tgfx
