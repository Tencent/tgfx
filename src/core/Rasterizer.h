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

#include "core/GlyphRunList.h"
#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * An ImageGenerator that can take vector graphics (paths, texts) and convert them into a raster
 * image.
 */
class Rasterizer : public ImageGenerator {
 public:
  /**
   * Creates a Rasterizer from a GlyphRunList.
   */
  static std::shared_ptr<Rasterizer> MakeFrom(int width, int height,
                                              std::shared_ptr<GlyphRunList> glyphRunList,
                                              bool antiAlias, const Matrix& matrix,
                                              const Stroke* stroke = nullptr);
  /**
   * Creates a Rasterizer from a Path.
   */
  static std::shared_ptr<Rasterizer> MakeFrom(int width, int height, Path path, bool antiAlias,
                                              const Matrix& matrix, const Stroke* stroke = nullptr);

  /**
   * Creates a Rasterizer from a Shape.
   */
  static std::shared_ptr<Rasterizer> MakeFrom(int width, int height, std::shared_ptr<Shape> shape,
                                              bool antiAlias);

  bool isAlphaOnly() const override {
    return true;
  }

  bool asyncSupport() const override;

 protected:
  Rasterizer(int width, int height) : ImageGenerator(width, height) {
  }
};
}  // namespace tgfx
