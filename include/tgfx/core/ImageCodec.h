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

#include "tgfx/core/Data.h"
#include "tgfx/core/EncodedFormat.h"
#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Orientation.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/platform/NativeImage.h"

namespace tgfx {

class ImageBuffer;

/**
 * ImageCodec is an abstract class that defines the interface for decoding images.
 */
class ImageCodec : public ImageGenerator {
 public:
  /**
   * If this file path represents an encoded image that we know how to decode, return an ImageCodec
   * that can decode it. Otherwise, return nullptr.
   */
  static std::shared_ptr<ImageCodec> MakeFrom(const std::string& filePath);

  /**
   * If the file bytes represent an encoded image that we know how to decode, return an ImageCodec
   * that can decode it. Otherwise, return nullptr.
   */
  static std::shared_ptr<ImageCodec> MakeFrom(std::shared_ptr<Data> imageBytes);

  /**
   * Creates a new ImageCodec using the provided ImageInfo and pixel data from an immutable Data
   * object. The returned ImageCodec holds a reference to the pixel data, so the caller must ensure
   * the pixels remain unchanged for the lifetime of the ImageCodec. Returns nullptr if ImageInfo is
   * empty or pixels is nullptr.
   */
  static std::shared_ptr<ImageCodec> MakeFrom(
      const ImageInfo& info, std::shared_ptr<Data> pixels,
      std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Creates a new ImageCodec object from a platform-specific NativeImage. For example, the
   * NativeImage could be a jobject that represents a java Bitmap on the android platform or a
   * CGImageRef on the apple platform. The returned ImageCodec object takes a reference to the
   * nativeImage. Returns nullptr if the nativeImage is nullptr or the current platform has no
   * NativeImage support.
   */
  static std::shared_ptr<ImageCodec> MakeFrom(NativeImageRef nativeImage);

  /**
   * Encodes the specified Pixmap into a binary image format. Returns nullptr if encoding fails.
   */
  static std::shared_ptr<Data> Encode(
      const Pixmap& pixmap, EncodedFormat format, int quality,
      std::shared_ptr<ColorSpace> colorSpace = ColorSpace::MakeSRGB());

  /**
   * Returns the orientation of the target image.
   */
  Orientation orientation() const {
    return _orientation;
  }

  bool isAlphaOnly() const override {
    return false;
  }

  bool isImageCodec() const final {
    return true;
  }

  /**
  * Decodes the image into the given pixel buffer using the specified image info. If the size
  * in dstInfo differs from the codec's size, this method will attempt to downscale the image
  * using a box filter algorithm to fit dstInfo. Only downscaling is supported. Returns true
  * if decoding succeeds, false otherwise.
  */
  virtual bool readPixels(const ImageInfo& dstInfo, void* dstPixels) const;

 protected:
  ImageCodec(int width, int height, Orientation orientation = Orientation::TopLeft,
             std::shared_ptr<ColorSpace> colorSpace = nullptr)
      : ImageGenerator(width, height, std::move(colorSpace)), _orientation(orientation) {
  }

  std::shared_ptr<ImageBuffer> onMakeBuffer(bool tryHardware) const override;

  virtual bool onReadPixels(ColorType colorType, AlphaType alphaType, size_t dstRowBytes,
                            void* dstPixels) const = 0;

  virtual std::shared_ptr<Data> getEncodedData() const {
    return nullptr;
  };

 private:
  Orientation _orientation = Orientation::TopLeft;

  /**
   * If the file path represents an encoded image that the current platform knows how to decode,
   * returns an ImageCodec that can decode it. Otherwise, returns nullptr.
   */
  static std::shared_ptr<ImageCodec> MakeNativeCodec(const std::string& filePath);

  /**
   * If the file bytes represent an encoded image that the current platform knows how to decode,
   * returns an ImageCodec that can decode it. Otherwise, returns nullptr.
   */
  static std::shared_ptr<ImageCodec> MakeNativeCodec(std::shared_ptr<Data> imageBytes);

  friend class Pixmap;
  friend class SVGExportContext;
  friend class PDFBitmap;
};
}  // namespace tgfx
