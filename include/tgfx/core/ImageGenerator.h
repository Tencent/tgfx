/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/ImageBuffer.h"

namespace tgfx {
/**
 * ImageGenerator defines the interfaces for generating ImageBuffer objects from encoded images or
 * custom data.
 */
class ImageGenerator {
 public:
  virtual ~ImageGenerator() = default;

  /**
   * Returns the width of the target image.
   */
  int width() const {
    return _width;
  }

  /**
   * Returns the height of the target image.
   */
  int height() const {
    return _height;
  }

  /**
   * Returns true if the generator is guaranteed to produce transparency only pixels. If true, each
   * pixel is packed in 8 bits as defined by ColorType::ALPHA_8.
   */
  virtual bool isAlphaOnly() const = 0;

  /**
   * Returns true if the ImageGenerator supports asynchronous decoding. If so, the makeBuffer()
   * method can be called from an arbitrary thread. Otherwise, the makeBuffer() method must be
   * called from the main thread.
   */
  virtual bool asyncSupport() const {
    return true;
  }

  /**
   * Returns true if this ImageGenerator is an ImageCodec, meaning it can read pixels directly from
   * the decoded image buffer. If false, the ImageGenerator is a custom generator and cannot read
   * pixels directly.
   */
  virtual bool isImageCodec() const {
    return false;
  }

  /**
   * Crates a new image buffer capturing the pixels decoded from this image generator.
   * ImageGenerator does not cache the returned image buffer, each call to this method allocates
   * additional storage. Returns an ImageBuffer backed by hardware if tryHardware is true and
   * the current platform supports creating it. Otherwise, a raster ImageBuffer is returned.
   */
  std::shared_ptr<ImageBuffer> makeBuffer(bool tryHardware = true) const {
    return onMakeBuffer(tryHardware);
  }

  std::shared_ptr<ColorSpace> colorSpace() const {
    return _colorSpace;
  }

  void setColorSpace(std::shared_ptr<ColorSpace> colorSpace) {
    _colorSpace = std::move(colorSpace);
  }

 protected:
  ImageGenerator(int width, int height,
                 std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB())
      : _width(width), _height(height), _colorSpace(std::move(colorSpace)) {
  }

  virtual std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const = 0;

 private:
  int _width = 0;
  int _height = 0;
  std::shared_ptr<ColorSpace> _colorSpace = ColorSpace::MakeSRGB();
};
}  // namespace tgfx
