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
#include "tgfx/core/Mask.h"

namespace tgfx {
class ShapeRasterizer;

/**
 * Rasterizer is the base class for converting vector graphics (shapes and glyphs) into their
 * rasterized forms.
 */
class Rasterizer : public ImageGenerator {
 public:
  /**
   * Creates a Rasterizer from a GlyphRunList.
   */
  static std::shared_ptr<Rasterizer> MakeFrom(std::shared_ptr<GlyphRunList> glyphRunList,
                                              const ISize& clipSize, const Matrix& matrix,
                                              bool antiAlias, const Stroke* stroke = nullptr);
  /**
   * Creates a Rasterizer from a Path.
   */
  static std::shared_ptr<ShapeRasterizer> MakeFrom(Path path, const ISize& clipSize,
                                                   const Matrix& matrix, bool antiAlias,
                                                   const Stroke* stroke = nullptr);

  ~Rasterizer() override;

  bool isAlphaOnly() const override {
    return true;
  }

  bool asyncSupport() const override;

 protected:
  Matrix matrix = Matrix::I();
  bool antiAlias = true;
  Stroke* stroke = nullptr;

  Rasterizer(const ISize& clipSize, const Matrix& matrix, bool antiAlias, const Stroke* stroke);
};
}  // namespace tgfx
