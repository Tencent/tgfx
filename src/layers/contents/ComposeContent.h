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
   * @param hasContourTable Whether contours and paintedContours hold a representative per group of
   * entries sharing one geometry. When false, the queries read every entry instead, so an empty
   * table must not be confused with a table that selects nothing.
   * @param contours The representative of every group for a query that includes the geometries no
   * paint captured.
   * @param paintedContours The representative of every group for a query that excludes them.
   * Grouping never mixes the two kinds, so a group is painted or bare as a whole: this table is
   * empty where every group is bare, which lets the selection filter test the entries one by one.
   */
  ComposeContent(std::vector<std::unique_ptr<GeometryContent>> contents,
                 size_t foregroundStartIndex, bool hasContourTable,
                 std::vector<GeometryContent*> contours,
                 std::vector<GeometryContent*> paintedContours);

  Rect getBounds(bool includeUnpaintedGeometry) const override;
  Rect getTightBounds(const Matrix& matrix, bool includeUnpaintedGeometry) const override;
  bool hitTestPoint(float localX, float localY, bool includeUnpaintedGeometry) const override;
  bool contourEqualsOpaqueContent() const override;
  bool hasBlendMode() const override;
  bool hasPaintedContent() const override;
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
    if (!hasContourTable) {
      return nullptr;
    }
    return includeUnpaintedGeometry ? &contours : &paintedContours;
  }

  std::vector<std::unique_ptr<GeometryContent>> contents = {};
  size_t foregroundStartIndex = 0;
  bool hasContourTable = false;
  std::vector<GeometryContent*> contours = {};
  std::vector<GeometryContent*> paintedContours = {};
};

}  // namespace tgfx
