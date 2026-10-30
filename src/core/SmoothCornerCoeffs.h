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

namespace tgfx {
// Per-unit-radius coefficients of a smoothed right-angle corner. Every length of the corner
// scales linearly with the corner radius, so they are stored as ratios that depend only on
// smoothness.
struct SmoothCornerCoeffs {
  // Ratio of the corner footprint (tangent point to corner vertex) to the radius: 1 + s.
  float tangentRatio = 1.0f;
  // Offsets of the arc start point from the tangent-line intersection, along and perpendicular to
  // the edge, as ratios of the radius.
  float endOffsetX = 0.0f;
  float endOffsetY = 0.0f;
  // Ratio of the arc chord length (vertex to arc start) to the radius.
  float arcChordRatio = 0.0f;
  // Ratio of the control-point step along the edge to the radius. The two control points sit at
  // 2 and 3 steps from the tangent point.
  float controlStep = 0.0f;
  // Ratio of the arc bezier handle length to the radius.
  float arcHandle = 0.0f;
  // Sine and cosine of the transition turn angle theta = pi*s/4. The join-point tangents and
  // normals used for territory selection are built from them.
  float turnSin = 0.0f;
  float turnCos = 1.0f;
};

// Computes the coefficients for the given smoothness, which is clamped to [0, 1].
SmoothCornerCoeffs ComputeSmoothCornerCoeffs(float smoothness);
}  // namespace tgfx
