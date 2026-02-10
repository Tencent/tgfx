/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/Path.h"

namespace tgfx {
/**
 * Provides utility functions for converting paths to a collection of triangles.
 */
class PathTriangulator {
 public:
  /**
   * Determines if the path should be triangulated instead of rasterized as an image, based on the
   * shape's complexity. This aims to balance performance and memory usage.
   */
  static bool ShouldTriangulatePath(const Path& path);

  /**
   * Returns the number of vertices based on the buffer size. Each vertex has 2 floats (x, y).
   */
  static size_t GetNonAAVertexCount(size_t bufferSize);

  /**
   * Tessellates the path into a collection of triangles. Returns the number of triangles written
   * to the vertices.
   */
  static size_t ToTriangles(const Path& path, const Rect& clipBounds, std::vector<float>* vertices,
                            bool* isLinear = nullptr);

  /**
   * Returns the number of vertices for antialiasing triangles based on the buffer size. Each vertex
   * has 3 floats (x, y, coverage).
   */
  static size_t GetAAVertexCount(size_t bufferSize);

  /**
   * Triangulates the given path in device space with a mesh of alpha ramps for antialiasing.
   * Returns the number of triangles written to the vertices.
   */
  static size_t ToAATriangles(const Path& path, const Rect& clipBounds,
                              std::vector<float>* vertices);
};
}  // namespace tgfx
