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

#include "gpu/QuadCW.h"
#include "tgfx/core/Color.h"

namespace tgfx {

/**
 * AA flags for each edge of a quad. Edge is defined by vertex indices in clockwise order.
 */
static constexpr unsigned QUAD_AA_FLAG_EDGE_01 = 0b0001;  // Edge from vertex 0 to vertex 1
static constexpr unsigned QUAD_AA_FLAG_EDGE_12 = 0b0010;  // Edge from vertex 1 to vertex 2
static constexpr unsigned QUAD_AA_FLAG_EDGE_23 = 0b0100;  // Edge from vertex 2 to vertex 3
static constexpr unsigned QUAD_AA_FLAG_EDGE_30 = 0b1000;  // Edge from vertex 3 to vertex 0
static constexpr unsigned QUAD_AA_FLAG_NONE = 0b0000;
static constexpr unsigned QUAD_AA_FLAG_ALL = 0b1111;

/**
 * QuadRecord stores a CW-ordered quad with per-edge AA flags for rendering.
 * Vertices are in clockwise order. For triangles, point(2) == point(3).
 */
struct QuadRecord {
  QuadRecord() = default;

  QuadRecord(const QuadCW& quad, unsigned aaFlags, const Color& color = {})
      : quad(quad), aaFlags(aaFlags), color(color) {}

  /**
   * Four vertices in clockwise order.
   */
  QuadCW quad;

  /**
   * Per-edge AA flags.
   */
  unsigned aaFlags = QUAD_AA_FLAG_NONE;

  /**
   * Vertex color for blending.
   */
  Color color = {};
};

}  // namespace tgfx
