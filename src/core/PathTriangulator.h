/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "DataProvider.h"
#include "tgfx/core/Path.h"

namespace tgfx {
// https://chromium-review.googlesource.com/c/chromium/src/+/1099564/
static constexpr int AA_TESSELLATOR_MAX_VERB_COUNT = 100;

/**
 * A DataProvider which tessellates a Path in device space with a mesh of alpha ramps for
 * antialiasing.
 */
class PathTriangulator : public DataProvider {
 public:
  /**
   * When tessellating curved paths into linear segments, this defines the maximum distance in
   * screen space which a segment may deviate from the mathematically correct value. Above this
   * value, the segment will be subdivided. This value was chosen to approximate the super sampling
   * accuracy of the raster path (16 samples, or one quarter pixel).
   */
  static constexpr float DefaultTolerance = 0.25f;

  /**
   * Returns the number of antialiasing triangles based on the buffer size of the vertices.
   */
  static int GetAATriangleCount(size_t bufferSize);

  PathTriangulator(Path path, const Rect& clipBounds, float tolerance = DefaultTolerance);

  /**
   * Tessellates the path into triangles. Returns the number of triangles written to the vertices.
   */
  int toTriangles(std::vector<float>* vertices) const;

  std::shared_ptr<Data> getData() const override;

 private:
  Path path = {};
  Rect clipBounds = Rect::MakeEmpty();
  float tolerance = DefaultTolerance;
};
}  // namespace tgfx
