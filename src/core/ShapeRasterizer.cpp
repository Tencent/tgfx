/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
#include "../../include/tgfx/core/Log.h"
#include "core/PathRasterizer.h"
#include "core/PathTriangulator.h"

namespace tgfx {
ShapeRasterizer::ShapeRasterizer(int width, int height, std::shared_ptr<Shape> shape, AAType aaType)
    : width(width), height(height), shape(std::move(shape)), aaType(aaType) {
}

bool ShapeRasterizer::asyncSupport() const {
#if defined(TGFX_BUILD_FOR_WEB) && !defined(TGFX_USE_FREETYPE)
  return false;
#else
  return true;
#endif
}

std::shared_ptr<ShapeBuffer> ShapeRasterizer::getData() const {
  auto finalPath = shape->getPath();
  if (finalPath.isEmpty() && finalPath.isInverseFillType()) {
    finalPath.reset();
    finalPath.addRect(Rect::MakeWH(width, height));
  }
  if (PathTriangulator::ShouldTriangulatePath(finalPath)) {
    auto triangles = makeTriangles(finalPath);
    if (triangles == nullptr) {
      return nullptr;
    }
    return std::make_shared<ShapeBuffer>(triangles, nullptr);
  }
  auto imageBuffer = makeImageBuffer(finalPath);
  if (imageBuffer == nullptr) {
    return nullptr;
  }
  return std::make_shared<ShapeBuffer>(nullptr, imageBuffer);
}

std::shared_ptr<Data> ShapeRasterizer::makeTriangles(const Path& finalPath) const {
  std::vector<float> vertices = {};
  size_t count = 0;
  auto bounds = Rect::MakeWH(width, height);
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

std::shared_ptr<ImageBuffer> ShapeRasterizer::makeImageBuffer(const Path& finalPath) const {
  auto pathRasterizer = PathRasterizer::MakeFrom(width, height, finalPath, aaType != AAType::None);
  if (pathRasterizer == nullptr) {
    LOGE("ShapeRasterizer::makeImageBuffer() Failed to create the PathRasterizer!");
    return nullptr;
  }
  return pathRasterizer->makeBuffer();
}
}  // namespace tgfx
