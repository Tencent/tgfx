/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/Quad.h"
#include "tgfx/core/Color.h"

namespace tgfx {

/**
 * AA flags for each edge of a quad. Each flag represents the edge starting from the vertex.
 *
 * Vertex and edge layout:
 *   0 ←-- 2
 *   ↓     ↑
 *   1 --→ 3
 */
static constexpr unsigned QUAD_AA_FLAG_EDGE_0 = 0b0001;
static constexpr unsigned QUAD_AA_FLAG_EDGE_1 = 0b0010;
static constexpr unsigned QUAD_AA_FLAG_EDGE_2 = 0b0100;
static constexpr unsigned QUAD_AA_FLAG_EDGE_3 = 0b1000;
static constexpr unsigned QUAD_AA_FLAG_NONE = 0b0000;
static constexpr unsigned QUAD_AA_FLAG_ALL = 0b1111;

/**
 * QuadRecord stores a Z-order quad with per-edge AA flags and optional transform matrix.
 */
struct QuadRecord {
  QuadRecord(const Quad& quad, unsigned aaFlags, const Color& color = {},
             const Matrix& matrix = Matrix::I())
      : quad(quad), aaFlags(aaFlags), color(color), matrix(matrix) {
  }

  /**
   * Four vertices in Z-order.
   */
  Quad quad;

  /**
   * Per-edge AA flags.
   */
  unsigned aaFlags = QUAD_AA_FLAG_NONE;

  /**
   * Vertex color for blending.
   */
  Color color = {};

  /**
   * Transform matrix applied to quad vertices.
   */
  Matrix matrix = Matrix::I();
};

}  // namespace tgfx
