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

#include "core/utils/Log.h"
#include "tgfx/core/Point.h"

namespace tgfx {

/**
 * QuadCW represents a quadrilateral with vertices in clockwise order.
 * Used for logical quad operations where edge connectivity matters.
 *
 * Vertex layout (clockwise):
 *   p0 -----> p1
 *   ^          |
 *   |          v
 *   p3 <----- p2
 *
 * Edge definitions (matching QUAD_AA_FLAG_EDGE_*):
 *   EDGE_01: p0 -> p1
 *   EDGE_12: p1 -> p2
 *   EDGE_23: p2 -> p3
 *   EDGE_30: p3 -> p0
 */
class QuadCW {
 public:
  constexpr QuadCW() = default;

  constexpr QuadCW(const Point& p0, const Point& p1, const Point& p2, const Point& p3)
      : points{p0, p1, p2, p3} {
  }

  const Point& point(size_t i) const {
    DEBUG_ASSERT(i < 4);
    return points[i];
  }

 private:
  Point points[4] = {};
};

}  // namespace tgfx
