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

#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/Mask.h"

namespace tgfx {
/**
 * An ImageGenerator that can take vector graphics (paths, texts) and convert them into a raster
 * image.
 */
class Rasterizer : public ImageGenerator {
 public:
  /**
   * Creates a Rasterizer from a TextBlob.
   */
  static std::shared_ptr<Rasterizer> MakeFrom(std::shared_ptr<TextBlob> textBlob,
                                              const ISize& clipSize, const Matrix& matrix,
                                              const Stroke* stroke = nullptr);

  /**
   * Creates a Rasterizer from a Path.
   */
  static std::shared_ptr<Rasterizer> MakeFrom(Path path, const ISize& clipSize,
                                              const Matrix& matrix, const Stroke* stroke = nullptr);

  virtual ~Rasterizer();

  bool isAlphaOnly() const override {
    return true;
  }

 protected:
  Rasterizer(const ISize& clipSize, const Matrix& matrix, const Stroke* stroke);

  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override;

  virtual void onRasterize(Mask* mask, const Stroke* stroke) const = 0;

 private:
  Matrix matrix = Matrix::I();
  Stroke* strokeStyle = nullptr;
};
}  // namespace tgfx
