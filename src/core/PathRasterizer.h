/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/GlyphFace.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * A Rasterizer that rasterizes a give shape.
 */
class PathRasterizer : public ImageCodec {
 public:
  /**
   * Creates a new PathRasterizer instance with the specified width, height, shape, anti-aliasing
   * setting, and gamma correction flag. Anti-aliasing and gamma correction are recommended for glyph
   * path rendering, while gamma correction is generally unnecessary for regular path rendering.
   */
  static std::shared_ptr<PathRasterizer> Make(int width, int height, std::shared_ptr<Shape> shape,
                                              bool antiAlias, bool needsGammaCorrection = false);

  bool isAlphaOnly() const override {
    return true;
  }

  bool readPixels(const ImageInfo& dstInfo, void* dstPixels) const override = 0;

 protected:
  explicit PathRasterizer(int width, int height, std::shared_ptr<Shape> shape, bool antiAlias,
                          bool needsGammaCorrection);

  static void ClearPixels(const ImageInfo& dstInfo, void* dstPixels);

  std::shared_ptr<Shape> shape = nullptr;
  bool antiAlias = false;
  bool needsGammaCorrection = false;
};
}  // namespace tgfx
