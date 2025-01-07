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

#pragma once

#include "tgfx/core/Image.h"
#include "tgfx/layers/ShapeStyle.h"

namespace tgfx {
/**
 * ImagePattern describes a pattern based on an image, which can be drawn on a shape layer. The
 * image can be repeated in both the x and y directions, and you can specify the sampling options.
 */
class ImagePattern : public ShapeStyle {
 public:
  /**
    * Creates a new ImagePattern with the given image, tile modes, and sampling options.
    * @param image The image to use for the pattern.
    * @param tileModeX The tile mode for the x direction.
    * @param tileModeY The tile mode for the y direction.
    * @param sampling The sampling options to use when sampling the image.
    * @return A new ImagePattern, nullptr if the image is nullptr.
    */
  static std::shared_ptr<ImagePattern> Make(std::shared_ptr<Image> image,
                                            TileMode tileModeX = TileMode::Clamp,
                                            TileMode tileModeY = TileMode::Clamp,
                                            const SamplingOptions& sampling = {});

  /**
   * Returns the transformation matrix applied to the ImagePattern.
   */
  const Matrix& matrix() const {
    return _matrix;
  }

  /**
   * Sets the transformation matrix applied to the ImagePattern.
   */
  void setMatrix(const Matrix& value);

 protected:
  std::shared_ptr<Shader> getShader() const override;

 private:
  std::shared_ptr<Image> image = nullptr;
  TileMode tileModeX = TileMode::Clamp;
  TileMode tileModeY = TileMode::Clamp;
  SamplingOptions sampling = {};
  Matrix _matrix = Matrix::I();

  ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
               const SamplingOptions& sampling);
};
}  // namespace tgfx
