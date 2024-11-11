/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ShapeRasterizer.h"
#include "core/PathTriangulator.h"
#include "tgfx/core/Mask.h"

namespace tgfx {
// https://chromium-review.googlesource.com/c/chromium/src/+/1099564/
static constexpr int AA_TESSELLATOR_MAX_VERB_COUNT = 100;
// A factor used to estimate the memory size of a tessellated path, based on the average value of
// Buffer.size() / Path.countPoints() from 4300+ tessellated path data.
static constexpr int AA_TESSELLATOR_BUFFER_SIZE_FACTOR = 170;

bool ShapeRasterizer::ShouldTriangulatePath(const Path& path) {
  if (path.countVerbs() <= AA_TESSELLATOR_MAX_VERB_COUNT) {
    return true;
  }
  auto bounds = path.getBounds();
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  return path.countPoints() * AA_TESSELLATOR_BUFFER_SIZE_FACTOR <= width * height;
}

std::shared_ptr<ShapeRasterizer> Rasterizer::MakeFrom(Path path, const ISize& clipSize,
                                                      const Matrix& matrix, bool antiAlias,
                                                      const Stroke* stroke) {
  if (path.isEmpty() || clipSize.isEmpty()) {
    return nullptr;
  }
  return std::shared_ptr<ShapeRasterizer>(
      new ShapeRasterizer(std::move(path), clipSize, matrix, antiAlias, stroke));
}

ShapeRasterizer::ShapeRasterizer(Path path, const ISize& clipSize, const Matrix& matrix,
                                 bool antiAlias, const Stroke* stroke)
    : Rasterizer(clipSize, matrix, antiAlias, stroke), path(std::move(path)) {
}

std::shared_ptr<ShapeBuffer> ShapeRasterizer::makeRasterized(bool tryHardware) const {
  auto finalPath = getFinalPath();
  if (ShouldTriangulatePath(finalPath)) {
    return ShapeBuffer::MakeFrom(makeTriangles(finalPath));
  }
  return ShapeBuffer::MakeFrom(makeImageBuffer(finalPath, tryHardware));
}

std::shared_ptr<ImageBuffer> ShapeRasterizer::onMakeBuffer(bool tryHardware) const {
  auto finalPath = getFinalPath();
  return makeImageBuffer(finalPath, tryHardware);
}

Path ShapeRasterizer::getFinalPath() const {
  auto finalPath = path;
  if (stroke != nullptr) {
    stroke->applyToPath(&finalPath);
  }
  finalPath.transform(matrix);
  return finalPath;
}

std::shared_ptr<Data> ShapeRasterizer::makeTriangles(const Path& finalPath) const {
  std::vector<float> vertices = {};
  size_t count = 0;
  auto bounds = Rect::MakeWH(width(), height());
  if (antiAlias) {
    count = PathTriangulator::ToAATriangles(finalPath, bounds, &vertices);
  } else {
    count = PathTriangulator::ToTriangles(finalPath, bounds, &vertices);
  }
  if (count == 0) {
    // The path is not a filled path, or it is invisible.
    return nullptr;
  }
  return Data::MakeWithCopy(vertices.data(), vertices.size() * sizeof(float));
}

std::shared_ptr<ImageBuffer> ShapeRasterizer::makeImageBuffer(const Path& finalPath,
                                                              bool tryHardware) const {
  auto mask = Mask::Make(width(), height(), tryHardware);
  if (!mask) {
    return nullptr;
  }
  mask->setAntiAlias(antiAlias);
  mask->fillPath(finalPath);
  return mask->makeBuffer();
}
}  // namespace tgfx
