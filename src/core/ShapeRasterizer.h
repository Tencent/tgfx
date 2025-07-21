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

#pragma once

#include "DataSource.h"
#include "gpu/AAType.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
struct ShapeBuffer {
  ShapeBuffer(std::shared_ptr<Data> triangles, std::shared_ptr<ImageBuffer> imageBuffer)
      : triangles(std::move(triangles)), imageBuffer(std::move(imageBuffer)) {
  }

  std::shared_ptr<Data> triangles = nullptr;
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
};

/**
 * ShapeRasterizer converts a shape into its rasterized form.
 */
class ShapeRasterizer : public DataSource<ShapeBuffer> {
 public:
  /**
   * Creates a ShapeRasterizer from a shape.
   */
  ShapeRasterizer(int width, int height, std::shared_ptr<Shape> shape, AAType aaType);

  /**
   * Returns true if the ShapeRasterizer supports asynchronous decoding. If so, the getData()
   * method can be called from an arbitrary thread. Otherwise, the getData() method must be
   * called from the main thread.
   */
  bool asyncSupport() const;

  /**
   * Rasterizes the shape into a ShapeBuffer. Unlike the makeBuffer() method, which always returns
   * an image buffer, this method returns a ShapeBuffer that may contain either a triangle mesh or
   * an image buffer, depending on the shape's complexity. This method aims to balance performance
   * and memory usage. Returns nullptr if rasterization fails.
   */
  std::shared_ptr<ShapeBuffer> getData() const override;

 private:
  int width = 0;
  int height = 0;
  std::shared_ptr<Shape> shape = nullptr;
  AAType aaType = AAType::None;

  std::shared_ptr<Data> makeTriangles(const Path& finalPath) const;

  std::shared_ptr<ImageBuffer> makeImageBuffer(const Path& finalPath) const;
};
}  // namespace tgfx
