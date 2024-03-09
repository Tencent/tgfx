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

#include "PathTriangulator.h"
#include "PathRef.h"
#include "pathkit.h"

namespace tgfx {
/**
 * When tessellating curved paths into linear segments, this defines the maximum distance in
 * screen space which a segment may deviate from the mathematically correct value. Above this
 * value, the segment will be subdivided. This value was chosen to approximate the super sampling
 * accuracy of the raster path (16 samples, or one quarter pixel).
 */
static constexpr float DefaultTolerance = 0.25f;

size_t PathTriangulator::GetTriangleCount(size_t bufferSize) {
  return bufferSize / (sizeof(float) * 2);
}

size_t PathTriangulator::ToTriangles(const Path& path, const Rect& clipBounds,
                                     std::vector<float>* vertices, bool* isLinear) {
  const auto& skPath = PathRef::ReadAccess(path);
  bool linear = false;
  auto count = skPath.toTriangles(
      DefaultTolerance, *reinterpret_cast<const pk::SkRect*>(&clipBounds), vertices, &linear);
  if (isLinear) {
    *isLinear = linear;
  }
  return static_cast<size_t>(count);
}

size_t PathTriangulator::GetAATriangleCount(size_t bufferSize) {
  return bufferSize / (sizeof(float) * 3);
}

size_t PathTriangulator::ToAATriangles(const Path& path, const Rect& clipBounds,
                                       std::vector<float>* vertices) {
  const auto& skPath = PathRef::ReadAccess(path);
  auto count = skPath.toAATriangles(DefaultTolerance,
                                    *reinterpret_cast<const pk::SkRect*>(&clipBounds), vertices);
  return static_cast<size_t>(count);
}
}  // namespace tgfx
