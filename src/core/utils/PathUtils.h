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

#include <vector>
#include "tgfx/core/Point.h"

namespace tgfx {
class PathUtils {
 public:
  /**
   * When tessellating curved paths into linear segments, this defines the maximum distance in
   * screen space which a segment may deviate from the mathematically correct value. Above this
   * value, the segment will be subdivided. This value was chosen to approximate the super sampling
   * accuracy of the raster path (16 samples, or one quarter pixel).
   */
  static constexpr float DefaultTolerance = 0.25f;

  /**
   * Converts a cubic bezier curve into a sequence of quadratic bezier curves.
   *
   * cubicPoints is an array of 4 points defining the cubic curve.
   * tolerance is the maximum allowed deviation from the original curve.
   */
  static std::vector<Point> ConvertCubicToQuads(const Point cubicPoints[4],
                                                float tolerance = DefaultTolerance);

  /**
   * Subdivides a quadratic bezier curve at parameter t.
   *
   * src is an array of 3 points defining the input quadratic curve.
   * dst is an array of 5 points to receive the two resulting quadratic curves.
   * t is the subdivision parameter in the range [0, 1].
   */
  static void ChopQuadAt(const Point src[3], Point dst[5], float t);

  /**
   * Finds the parameter t at the point of maximum curvature on a quadratic bezier curve.
   *
   * src is an array of 3 points defining the quadratic curve.
   * Returns the t value for the point of maximum curvature if it exists on the segment,
   * otherwise returns 0.
   */
  static float FindQuadMaxCurvature(const Point src[3]);

  /**
   * Subdivides a quadratic bezier curve at the point of maximum curvature if it exists.
   *
   * src is an array of 3 points defining the input quadratic curve.
   * dst is an array of 5 points to receive the subdivided curves.
   * Returns 1 if no subdivision occurred (dst[0..2] contains the original quad).
   * Returns 2 if subdivision occurred (dst[0..2] and dst[2..4] contain the two new quads).
   */
  static int ChopQuadAtMaxCurvature(const Point src[3], Point dst[5]);
};

class QuadUVMatrix {
 public:
  QuadUVMatrix() = default;

  /**
   * Initializes the matrix from quadratic bezier control points.
   *
   * controlPoints is an array of 3 points defining the quadratic curve.
   */
  explicit QuadUVMatrix(const Point controlPoints[3]);

  /**
   * Sets the matrix from quadratic bezier control points.
   *
   * controlPoints is an array of 3 points defining the quadratic curve.
   */
  void set(const Point controlPoints[3]);

  /**
   * Applies the matrix to vertex positions to compute UV coords.
   *
   * vertices is a pointer to the first vertex.
   * vertexCount is the number of vertices.
   * stride is the size of each vertex.
   * uvOffset is the offset of the UV values within each vertex.
   */
  void apply(void* vertices, int vertexCount, size_t stride, size_t uvOffset) const;

 private:
  float matrix[6];
};
}  // namespace tgfx