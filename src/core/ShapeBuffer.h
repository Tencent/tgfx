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

#include "tgfx/core/Data.h"
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
/**
 * ShapeBuffer is a container for the rasterized shape data, either as triangles or as an image
 * buffer.
 */
class ShapeBuffer {
 public:
  /**
   * Creates a new ShapeBuffer from a set of triangles. Returns nullptr if the triangles are empty.
   */
  static std::shared_ptr<ShapeBuffer> MakeFrom(std::shared_ptr<Data> triangles);

  /**
   * Creates a new ShapeBuffer from an image buffer. Returns nullptr if the image buffer is nullptr.
   */
  static std::shared_ptr<ShapeBuffer> MakeFrom(std::shared_ptr<ImageBuffer> imageBuffer);

  /**
   * Returns the triangles data of the shape buffer. Returns nullptr if the shape buffer is backed
   * by an image buffer.
   */
  std::shared_ptr<Data> triangles() const {
    return _triangles;
  }

  /**
   * Returns the image buffer of the shape buffer. Returns nullptr if the shape buffer is backed by
   * triangles.
   */
  std::shared_ptr<ImageBuffer> imageBuffer() const {
    return _imageBuffer;
  }

 private:
  std::shared_ptr<Data> _triangles = nullptr;
  std::shared_ptr<ImageBuffer> _imageBuffer = nullptr;

  explicit ShapeBuffer(std::shared_ptr<Data> triangles) : _triangles(std::move(triangles)) {
  }

  explicit ShapeBuffer(std::shared_ptr<ImageBuffer> imageBuffer)
      : _imageBuffer(std::move(imageBuffer)) {
  }
};
}  // namespace tgfx
