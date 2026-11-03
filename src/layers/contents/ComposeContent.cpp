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
                               size_t foregroundStartIndex, std::vector<GeometryContent*> contours,
                               std::optional<std::vector<GeometryContent*>> paintedContours)
    : contents(std::move(contents)), foregroundStartIndex(foregroundStartIndex),
      contours(std::move(contours)), paintedContours(std::move(paintedContours)) {
}

Rect ComposeContent::getBounds(bool includeUnpaintedGeometry) const {
  auto result = Rect::MakeEmpty();
  if (auto table = selectedContours(includeUnpaintedGeometry)) {
    for (const auto content : *table) {
      result.join(content->getBounds(includeUnpaintedGeometry));
    }
    return result;
  }
  for (const auto& content : contents) {
    result.join(content->getBounds(includeUnpaintedGeometry));
  }
  return result;
}

Rect ComposeContent::getTightBounds(const Matrix& matrix, bool includeUnpaintedGeometry) const {
  auto result = Rect::MakeEmpty();
  if (auto table = selectedContours(includeUnpaintedGeometry)) {
    for (const auto content : *table) {
      result.join(content->getTightBounds(matrix, includeUnpaintedGeometry));
    }
    return result;
  }
  for (const auto& content : contents) {
    result.join(content->getTightBounds(matrix, includeUnpaintedGeometry));
  }
  return result;
}

bool ComposeContent::hitTestPoint(float localX, float localY, bool includeUnpaintedGeometry) const {
  for (const auto& content : contents) {
    if (content->hitTestPoint(localX, localY, includeUnpaintedGeometry)) {
      return true;
    }
  }
  return false;
}

void ComposeContent::drawContourInternal(Canvas* canvas, bool antiAlias,
                                         bool includeUnpaintedGeometry) const {
  // The only entry that sees the whole outline table, so it forwards to the selected entries when a
  // subset was chosen and otherwise to every content. A caliber that excludes bare geometry selects
  // nothing for a bare group, so nothing is drawn there rather than falling back to the entries.
  if (auto table = selectedContours(includeUnpaintedGeometry)) {
    for (const auto geometry : *table) {
      geometry->drawContour(canvas, antiAlias, includeUnpaintedGeometry);
    }
    return;
  }
  for (const auto& content : contents) {
    content->drawContour(canvas, antiAlias, includeUnpaintedGeometry);
  }
}

bool ComposeContent::contourEqualsOpaqueContent(bool includeUnpaintedGeometry) const {
  for (const auto& content : contents) {
    if (!content->contourEqualsOpaqueContent(includeUnpaintedGeometry)) {
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
