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

#pragma once

#include <memory>
#include <optional>
#include <vector>
#include "layers/contents/GeometryContent.h"

namespace tgfx {

/**
 * ComposeContent combines multiple GeometryContent objects into a single unit. It manages a list of
 * contents and supports separating them into default content (drawn below children) and foreground
 * content (drawn above children).
 */
class ComposeContent : public LayerContent {
 public:
  /**
   * @param contents Every recorded entry, in draw order.
   * @param foregroundStartIndex Index of the first entry drawn above the children.
   * @param contours The representative of every group for a query that includes bare geometries.
   *                 Empty means no table was built, so the queries read every entry.
   * @param paintedContours Materialized only when the two calibers select differently, which needs
   *                        a bare group; nullopt means it equals contours.
   */
  ComposeContent(std::vector<std::unique_ptr<GeometryContent>> contents,
                 size_t foregroundStartIndex, std::vector<GeometryContent*> contours,
                 std::optional<std::vector<GeometryContent*>> paintedContours);

  Rect getBounds(bool includeUnpaintedGeometry) const override;
  Rect getTightBounds(const Matrix& matrix, bool includeUnpaintedGeometry) const override;
  bool hitTestPoint(float localX, float localY, bool includeUnpaintedGeometry) const override;
  bool contourEqualsOpaqueContent(bool includeUnpaintedGeometry) const override;
  bool hasBlendMode() const override;
  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const override;
  void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const override;

 protected:
  void drawContourInternal(Canvas* canvas, bool antiAlias,
                           bool includeUnpaintedGeometry) const override;

  Type type() const override {
    return Type::Compose;
  }

 private:
  // Returns the representative table of the requested caliber, or null when no table was built and
  // the caller has to read every entry.
  const std::vector<GeometryContent*>* selectedContours(bool includeUnpaintedGeometry) const {
    if (contours.empty()) {
      return nullptr;
    }
    if (includeUnpaintedGeometry) {
      return &contours;
    }
    return paintedContours.has_value() ? &*paintedContours : &contours;
  }

  std::vector<std::unique_ptr<GeometryContent>> contents = {};
  size_t foregroundStartIndex = 0;
  std::vector<GeometryContent*> contours = {};
  std::optional<std::vector<GeometryContent*>> paintedContours = std::nullopt;
};

}  // namespace tgfx
