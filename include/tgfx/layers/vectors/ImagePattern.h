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

#include "tgfx/core/Image.h"
#include "tgfx/layers/vectors/ColorSource.h"
#include "tgfx/layers/vectors/ScaleMode.h"

namespace tgfx {
/**
 * ImagePattern describes a pattern based on an image, which can be drawn on a vector layer. The
 * image can be repeated in both the x and y directions, and you can specify the sampling options.
 * By default, ImagePattern fits the image into each geometry's bounding box according to
 * scaleMode() (ScaleMode::LetterBox by default). Set the scale mode to ScaleMode::None to skip
 * per-geometry fitting and place the image directly in the layer's coordinate space.
 */
class ImagePattern : public ColorSource {
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
                                            TileMode tileModeX = TileMode::Decal,
                                            TileMode tileModeY = TileMode::Decal,
                                            const SamplingOptions& sampling = {});

  std::shared_ptr<Image> image() const {
    return _image;
  }

  TileMode tileModeX() const {
    return _tileModeX;
  }

  TileMode tileModeY() const {
    return _tileModeY;
  }

  SamplingOptions samplingOptions() const {
    return _sampling;
  }

  /**
   * Returns the transformation matrix applied to the image pattern. The matrix operates on the
   * image's pixel space (with the origin at the top-left of the image and axes aligned with the
   * image pixels) and is applied in every scale mode.
   */
  const Matrix& matrix() const {
    return _matrix;
  }

  /**
   * Sets the transformation matrix applied to the image pattern. See matrix() for details on the
   * coordinate space this matrix operates in.
   */
  void setMatrix(const Matrix& matrix);

  /**
   * Returns the rule used to fit the image into each geometry's bounding box. The default value
   * is ScaleMode::LetterBox. When set to ScaleMode::None, the image is placed directly in the
   * layer's coordinate space without per-geometry fitting.
   */
  ScaleMode scaleMode() const {
    return _scaleMode;
  }

  /**
   * Sets the rule used to fit the image into each geometry's bounding box. Setting this to
   * ScaleMode::None disables per-geometry fitting.
   */
  void setScaleMode(ScaleMode mode);

  std::shared_ptr<Shader> getShader() const override;

  bool fitsToGeometry() const override {
    return _scaleMode != ScaleMode::None;
  }

  Matrix getFitMatrix(const Rect& bounds) const override;

 protected:
  Type getType() const override {
    return Type::ImagePattern;
  }

 private:
  std::shared_ptr<Image> _image = nullptr;
  TileMode _tileModeX = TileMode::Decal;
  TileMode _tileModeY = TileMode::Decal;
  SamplingOptions _sampling = {};
  Matrix _matrix = {};
  ScaleMode _scaleMode = ScaleMode::LetterBox;

  ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
               const SamplingOptions& sampling);
};
}  // namespace tgfx
