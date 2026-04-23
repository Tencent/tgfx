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
#include "tgfx/layers/vectors/FillSpace.h"
#include "tgfx/layers/vectors/ScaleMode.h"

namespace tgfx {
/**
 * ImagePattern describes a pattern based on an image, which can be drawn on a shape layer. The
 * image can be repeated in both the x and y directions, and you can specify the sampling options.
 * By default, ImagePattern samples the image in a normalized 0-1 space (FillSpace::Relative) that
 * maps to each shape's bounding box, fitted according to scaleMode(). Call
 * setFillSpace(FillSpace::Absolute) to place the image in the layer's coordinate space using the
 * pattern's matrix instead.
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
   * Returns the coordinate space used to interpret this pattern's parameters. The default value is
   * FillSpace::Relative.
   */
  FillSpace fillSpace() const {
    return _fillSpace;
  }

  /**
   * Sets the coordinate space used to interpret this pattern's parameters. When set to
   * FillSpace::Relative, the image is fitted to each shape's bounding box according to scaleMode().
   * When set to FillSpace::Absolute, the image is placed in the layer's coordinate space using the
   * pattern's matrix.
   */
  void setFillSpace(FillSpace space);

  /**
   * Returns the rule used to scale the image into the shape's bounding box when the fill space is
   * FillSpace::Relative. The default value is ScaleMode::LetterBox.
   */
  ScaleMode scaleMode() const {
    return _scaleMode;
  }

  /**
   * Sets the rule used to scale the image into the shape's bounding box when the fill space is
   * FillSpace::Relative. The scale mode only affects rendering when fillSpace is
   * FillSpace::Relative; in Absolute mode the stored value is preserved but not used.
   */
  void setScaleMode(ScaleMode mode);

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

  bool useRelativeSpace() const override {
    return _fillSpace == FillSpace::Relative;
  }

  Matrix getRelativeMatrix(const Rect& bounds) const override;

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
  FillSpace _fillSpace = FillSpace::Relative;
  ScaleMode _scaleMode = ScaleMode::LetterBox;
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
