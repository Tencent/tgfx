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

#include "PathTriangulator.h"
#include <cstring>
#include "PathRef.h"
#include "pathkit.h"
#include "utils/MathExtra.h"
#include "utils/PathUtils.h"

namespace tgfx {

// https://chromium-review.googlesource.com/c/chromium/src/+/1099564/
static constexpr int AA_TESSELLATOR_MAX_VERB_COUNT = 100;

// A factor used to estimate the memory size of a tessellated path, based on the average value of
// Buffer.size() / Path.countPoints() from 4300+ tessellated path data.
static constexpr int AA_TESSELLATOR_BUFFER_SIZE_FACTOR = 170;

static constexpr int MAX_RASTERIZED_TEXTURE_SIZE = 4096;

static constexpr int MIN_TRIANGULATE_SIZE = 162;

bool PathTriangulator::ShouldTriangulatePath(const Path& path) {
  auto bounds = path.getBounds();
  auto width = FloatCeilToInt(bounds.width());
  auto height = FloatCeilToInt(bounds.height());
  auto maxDimension = std::max(width, height);
  auto minDimension = std::min(width, height);
  if (minDimension <= 0) {
    return true;
  }
  if (maxDimension <= MIN_TRIANGULATE_SIZE) {
    return false;
  }
  if (path.countVerbs() <= AA_TESSELLATOR_MAX_VERB_COUNT) {
    return true;
  }
  if (maxDimension > MAX_RASTERIZED_TEXTURE_SIZE) {
    return true;
  }
  return path.countPoints() * AA_TESSELLATOR_BUFFER_SIZE_FACTOR <= width * height;
}

// Appends a white color slot (one float's worth of UByte4Normalized bytes) to every vertex of a
// tessellated buffer that pathkit wrote with floatsPerVertex floats per vertex. The color is a
// constant filler: the triangle buffers are cached per shape and shared across draws, so the
// per-draw paint color rides the Color uniform instead (selected by the HasCommonColor runtime
// uniform) and the broadcast value is never read.
static void AppendWhiteColorSlots(std::vector<float>* vertices, size_t floatsPerVertex) {
  constexpr uint32_t White = 0xFFFFFFFFu;
  const size_t count = vertices->size() / floatsPerVertex;
  vertices->resize(count * (floatsPerVertex + 1));
  for (size_t vertex = count; vertex > 0; --vertex) {
    const size_t dstBase = (vertex - 1) * (floatsPerVertex + 1);
    const size_t srcBase = (vertex - 1) * floatsPerVertex;
    // Backward copy: early blocks shift forward into overlapping ranges (memmove semantics).
    for (size_t f = floatsPerVertex; f > 0; --f) {
      (*vertices)[dstBase + f - 1] = (*vertices)[srcBase + f - 1];
    }
    std::memcpy(&(*vertices)[dstBase + floatsPerVertex], &White, sizeof(float));
  }
}

size_t PathTriangulator::GetNonAAVertexCount(size_t bufferSize) {
  // Each vertex has 3 float slots: x, y and the white color filler.
  return bufferSize / (sizeof(float) * 3);
}

size_t PathTriangulator::ToTriangles(const Path& path, const Rect& clipBounds,
                                     std::vector<float>* vertices, bool* isLinear) {
  const auto& skPath = PathRef::ReadAccess(path);
  bool linear = false;
  auto count =
      skPath.toTriangles(PathUtils::DefaultTolerance,
                         *reinterpret_cast<const pk::SkRect*>(&clipBounds), vertices, &linear);
  if (isLinear) {
    *isLinear = linear;
  }
  AppendWhiteColorSlots(vertices, 2);
  return static_cast<size_t>(count);
}

size_t PathTriangulator::GetAAVertexCount(size_t bufferSize) {
  // Each vertex has 4 float slots: x, y, coverage and the white color filler.
  return bufferSize / (sizeof(float) * 4);
}

size_t PathTriangulator::ToAATriangles(const Path& path, const Rect& clipBounds,
                                       std::vector<float>* vertices) {
  const auto& skPath = PathRef::ReadAccess(path);
  auto count = skPath.toAATriangles(PathUtils::DefaultTolerance,
                                    *reinterpret_cast<const pk::SkRect*>(&clipBounds), vertices);
  AppendWhiteColorSlots(vertices, 3);
  return static_cast<size_t>(count);
}
}  // namespace tgfx
