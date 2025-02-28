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
#include "utils/Log.h"
#include "utils/Profiling.h"

namespace tgfx {
ShapeRasterizer::ShapeRasterizer(int width, int height, std::shared_ptr<Shape> shape, AAType aaType)
    : Rasterizer(width, height), shape(std::move(shape)), aaType(aaType) {
}

std::shared_ptr<ShapeBuffer> ShapeRasterizer::makeRasterized(bool tryHardware) const {
  TRACE_EVENT_NAME("VectorRasterized");
  auto finalPath = shape->getPath();
  if (finalPath.isEmpty() && finalPath.isInverseFillType()) {
    finalPath.reset();
    finalPath.addRect(Rect::MakeWH(width(), height()));
  }
  if (PathTriangulator::ShouldTriangulatePath(finalPath)) {
    return ShapeBuffer::MakeFrom(makeTriangles(finalPath));
  }
  return ShapeBuffer::MakeFrom(makeImageBuffer(finalPath, tryHardware));
}

std::shared_ptr<ImageBuffer> ShapeRasterizer::onMakeBuffer(bool tryHardware) const {
  auto finalPath = shape->getPath();
  return makeImageBuffer(finalPath, tryHardware);
}

std::shared_ptr<Data> ShapeRasterizer::makeTriangles(const Path& finalPath) const {
  std::vector<float> vertices = {};
  size_t count = 0;
  auto bounds = Rect::MakeWH(width(), height());
  if (aaType == AAType::Coverage) {
    count = PathTriangulator::ToAATriangles(finalPath, bounds, &vertices);
  } else {
    // If MSAA is enabled, we skip generating AA triangles since the shape will be drawn directly to
    // the screen.
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
    LOGE("ShapeRasterizer::makeImageBuffer() Failed to create the mask!");
    return nullptr;
  }
  mask->setAntiAlias(aaType != AAType::None);
  mask->fillPath(finalPath);
  return mask->makeBuffer();
}
}  // namespace tgfx
