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

namespace tgfx {
/**
 * ImagePattern describes a pattern based on an image, which can be drawn on a shape layer. The
 * image can be repeated in both the x and y directions, and you can specify the sampling options.
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
                                            TileMode tileModeX = TileMode::Clamp,
                                            TileMode tileModeY = TileMode::Clamp,
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
   * Returns the transformation matrix applied to the image pattern.
   */
  const Matrix& matrix() const {
    return _matrix;
  }

  /**
   * Sets the transformation matrix applied to the image pattern.
   */
  void setMatrix(const Matrix& matrix);

  /**
   * Returns the exposure adjustment value. The default value is 0 (no adjustment).
   */
  float exposure() const {
    return _exposure;
  }

  /**
   * Sets the exposure adjustment value in the range of -1.0 to 1.0. Positive values brighten the
   * image with highlight protection, negative values darken it with shadow preservation.
   */
  void setExposure(float value);

  /**
   * Returns the contrast adjustment value. The default value is 0 (no adjustment).
   */
  float contrast() const {
    return _contrast;
  }

  /**
   * Sets the contrast adjustment value in the range of -1.0 to 1.0.
   */
  void setContrast(float value);

  /**
   * Returns the saturation adjustment value. The default value is 0 (no adjustment).
   */
  float saturation() const {
    return _saturation;
  }

  /**
   * Sets the saturation adjustment value in the range of -1.0 to 1.0.
   */
  void setSaturation(float value);

  /**
   * Returns the color temperature adjustment value. The default value is 0 (no adjustment).
   */
  float temperature() const {
    return _temperature;
  }

  /**
   * Sets the color temperature adjustment value in the range of -1.0 to 1.0. Positive values shift
   * the image warmer (yellow), negative values shift it cooler (blue).
   */
  void setTemperature(float value);

  /**
   * Returns the tint adjustment value. The default value is 0 (no adjustment).
   */
  float tint() const {
    return _tint;
  }

  /**
   * Sets the tint adjustment value in the range of -1.0 to 1.0. Positive values shift the image
   * toward magenta, negative values shift it toward green.
   */
  void setTint(float value);

  /**
   * Returns the highlights adjustment value. The default value is 0 (no adjustment).
   */
  float highlights() const {
    return _highlights;
  }

  /**
   * Sets the highlights adjustment value in the range of -1.0 to 1.0.
   */
  void setHighlights(float value);

  /**
   * Returns the shadows adjustment value. The default value is 0 (no adjustment).
   */
  float shadows() const {
    return _shadows;
  }

  /**
   * Sets the shadows adjustment value in the range of -1.0 to 1.0.
   */
  void setShadows(float value);

  std::shared_ptr<Shader> getShader() const override;

 protected:
  Type getType() const override {
    return Type::ImagePattern;
  }

 private:
  std::shared_ptr<Image> _image = nullptr;
  TileMode _tileModeX = TileMode::Clamp;
  TileMode _tileModeY = TileMode::Clamp;
  SamplingOptions _sampling = {};
  Matrix _matrix = {};
  float _exposure = 0.0f;
  float _contrast = 0.0f;
  float _saturation = 0.0f;
  float _temperature = 0.0f;
  float _tint = 0.0f;
  float _highlights = 0.0f;
  float _shadows = 0.0f;

  ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
               const SamplingOptions& sampling);
};
}  // namespace tgfx
