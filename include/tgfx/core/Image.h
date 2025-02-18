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

#include "tgfx/core/Data.h"
#include "tgfx/core/ImageGenerator.h"
#include "tgfx/core/ImageInfo.h"
#include "tgfx/core/Orientation.h"
#include "tgfx/core/Picture.h"
#include "tgfx/core/Pixmap.h"
#include "tgfx/core/SamplingOptions.h"
#include "tgfx/core/TileMode.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/ImageOrigin.h"
#include "tgfx/platform/HardwareBuffer.h"
#include "tgfx/platform/NativeImage.h"

namespace tgfx {
class FPArgs;
class TPArgs;
class Context;
class ImageFilter;
class FragmentProcessor;
class TextureProxy;
class Paint;

/**
 * The Image class represents a two-dimensional array of pixels for drawing. These pixels can be
 * decoded in a raster ImageBuffer, encoded in compressed data streams or scalable drawing commands,
 * or located in GPU memory as a GPU texture. The Image class is thread-safe and immutable once
 * created. The width and height of an Image are always greater than zero. Attempting to create an
 * Image with zero width or height will return nullptr.
 */
class Image {
 public:
  /**
   * Creates an Image from the file path. An Image is returned if the format of the image file is
   * recognized and supported. Recognized formats vary by platform.
   */
  static std::shared_ptr<Image> MakeFromFile(const std::string& filePath);

  /**
   * Creates an Image from the encoded data. An Image is returned if the format of the encoded data
   * is recognized and supported. Recognized formats vary by platform.
   */
  static std::shared_ptr<Image> MakeFromEncoded(std::shared_ptr<Data> encodedData);

  /**
   * Creates an Image from the platform-specific NativeImage. For example, the NativeImage could be
   * a jobject that represents a java Bitmap on the android platform or a CGImageRef on the apple
   * platform. The returned Image object takes a reference to the nativeImage. Returns nullptr if
   * the nativeImage is nullptr or the current platform has no NativeImage support.
   */
  static std::shared_ptr<Image> MakeFrom(NativeImageRef nativeImage);

  /**
   * Creates an Image from the image generator. An Image is returned if the generator is not
   * nullptr. The image generator may wrap codec data or custom data.
   */
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<ImageGenerator> generator);

  /**
   * Creates an Image from the ImageInfo and shares pixels from the immutable Data object. The
   * returned Image takes a reference to the pixels. The caller must ensure the pixels are always
   * the same for the lifetime of the returned Image. If the ImageInfo is unsuitable for direct
   * texture uploading, the Image will internally create an ImageGenerator for pixel format
   * conventing instead of an ImageBuffer. Returns nullptr if the ImageInfo is empty or the pixels
   * are nullptr.
   */
  static std::shared_ptr<Image> MakeFrom(const ImageInfo& info, std::shared_ptr<Data> pixels);

  /**
   * Creates an Image from the Bitmap, sharing bitmap pixels. The Bitmap will allocate new internal
   * pixel memory and copy the original pixels into it if there is a subsequent call of pixel
   * writing to the Bitmap. Therefore, the content of the returned Image will always be the same.
   */
  static std::shared_ptr<Image> MakeFrom(const Bitmap& bitmap);

  /**
   * Creates an Image from the platform-specific hardware buffer. For example, the hardware buffer
   * could be an AHardwareBuffer on the android platform or a CVPixelBufferRef on the apple
   * platform. The returned Image takes a reference to the hardwareBuffer. The caller must
   * ensure the buffer content stays unchanged for the lifetime of the returned Image. The
   * colorSpace is ignored if the hardwareBuffer contains only one plane, which is not in the YUV
   * format. Returns nullptr if the hardwareBuffer is nullptr.
   */
  static std::shared_ptr<Image> MakeFrom(HardwareBufferRef hardwareBuffer,
                                         YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates an Image from the given picture with the specified width, height, and matrix.
   * The picture will be drawn onto the Image using the provided matrix. The picture is not
   * immediately turned into raster pixels; instead, the returned Image holds a reference to the
   * picture and defers rasterization until it is actually required. Note: This method may return a
   * different type of Image other than PictureImage if the picture is simple enough.
   * @param picture A stream of drawing commands.
   * @param width The width of the Image.
   * @param height The height of the Image.
   * @param matrix A Matrix to apply transformations to the picture.
   * @return An Image that matches the content when the picture is drawn with the specified
   * parameters.
   */
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Picture> picture, int width, int height,
                                         const Matrix* matrix = nullptr);

  /**
   * Creates an Image in the I420 format with the specified YUVData and the YUVColorSpace. Returns
   * nullptr if the yuvData is invalid.
   */
  static std::shared_ptr<Image> MakeI420(std::shared_ptr<YUVData> yuvData,
                                         YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates an Image in the NV12 format with the specified YUVData and the YUVColorSpace. Returns
   * nullptr if the yuvData is invalid.
   */
  static std::shared_ptr<Image> MakeNV12(std::shared_ptr<YUVData> yuvData,
                                         YUVColorSpace colorSpace = YUVColorSpace::BT601_LIMITED);

  /**
   * Creates an Image from the ImageBuffer, An Image is returned if the imageBuffer is not nullptr
   * and its dimensions are greater than zero.
   */
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<ImageBuffer> imageBuffer);

  /**
   * Creates an Image from the backendTexture associated with the context. The caller must ensure
   * the backendTexture stays valid and unchanged for the lifetime of the returned Image. An Image
   * is returned if the format of the backendTexture is recognized and supported. Recognized formats
   * vary by GPU back-ends.
   */
  static std::shared_ptr<Image> MakeFrom(Context* context, const BackendTexture& backendTexture,
                                         ImageOrigin origin = ImageOrigin::TopLeft);

  /**
   * Creates an Image from the backendTexture associated with the context, taking ownership of the
   * backendTexture. The backendTexture will be released when no longer needed. The caller must
   * ensure the backendTexture stays unchanged for the lifetime of the returned Image. An Image is
   * returned if the format of the backendTexture is recognized and supported. Recognized formats
   * vary by GPU back-ends.
   */
  static std::shared_ptr<Image> MakeAdopted(Context* context, const BackendTexture& backendTexture,
                                            ImageOrigin origin = ImageOrigin::TopLeft);

  virtual ~Image() = default;

  /**
   * Returns the width of the Image.
   */
  virtual int width() const = 0;

  /**
   * Returns pixel row count.
   */
  virtual int height() const = 0;

  /**
   * Returns true if pixels represent transparency only. If true, each pixel is packed in 8 bits as
   * defined by ColorType::ALPHA_8.
   */
  virtual bool isAlphaOnly() const = 0;

  /**
   * Returns true if the Image has mipmap levels. The flag was set by the makeMipmapped() method,
   * which may be ignored if the GPU or the associated image source doesn’t support mipmaps.
   */
  virtual bool hasMipmaps() const {
    return false;
  }

  /**
   * Returns true if the Image and all its children have been fully decoded. A fully decoded Image
   * means that its pixels are ready for drawing. On the other hand, if the Image requires decoding
   * or rasterization on the CPU side before drawing, it is not yet fully decoded.
   */
  virtual bool isFullyDecoded() const {
    return true;
  }

  /**
   * Returns true if the Image was created from a GPU texture.
   */
  virtual bool isTextureBacked() const {
    return false;
  }

  /**
   * Returns an Image backed by a GPU texture associated with the given context. If a corresponding
   * texture cache exists in the context, it returns an Image that wraps that texture. Otherwise, it
   * creates one immediately. If the Image is already texture-backed and the context is compatible
   * with the GPU texture, it returns the original Image. Otherwise, it returns nullptr. It's safe
   * to release the original Image to reduce CPU memory usage, as the returned Image holds a strong
   * reference to the texture cache.
   */
  virtual std::shared_ptr<Image> makeTextureImage(Context* context) const;

  /**
   * Retrieves the backend texture of the Image. Returns an invalid BackendTexture if the Image is
   * not backed by a Texture. If the origin is not nullptr, the origin of the backend texture is
   * returned.
   */
  virtual BackendTexture getBackendTexture(Context* context, ImageOrigin* origin = nullptr) const;

  /**
   * Returns a fully decoded Image from this Image. The returned Image shares the same GPU cache
   * with the original Image and immediately schedules an asynchronous decoding task, which will not
   * block the calling thread. If the Image is fully decoded or has a corresponding texture cache in
   * the specified context, the original Image is returned.
   */
  std::shared_ptr<Image> makeDecoded(Context* context = nullptr) const;

  /**
   * Returns an Image with mipmaps enabled or disabled. If mipmaps are already enabled or disabled,
   * the original Image is returned. If enabling or disabling mipmaps fails, nullptr is returned.
   */
  std::shared_ptr<Image> makeMipmapped(bool enabled) const;

  /**
   * Returns subset of Image. The subset must be fully contained by Image dimensions. The returned
   * Image always shares pixels and caches with the original Image. Returns nullptr if the subset is
   * empty, or the subset is not contained by bounds.
   */
  std::shared_ptr<Image> makeSubset(const Rect& subset) const;

  /**
   * Returns an Image with its origin transformed by the given Orientation. The returned Image
   * always shares pixels and caches with the original Image. If the orientation is
   * Orientation::TopLeft, the original Image is returned.
   */
  std::shared_ptr<Image> makeOriented(Orientation orientation) const;

  /**
   * Returns a rasterized Image scaled by the specified rasterizationScale. A rasterized Image can
   * be cached as an independent GPU resource for repeated drawing. By default, an Image directly
   * backed by an ImageBuffer, an ImageGenerator, a GPU texture, or a Picture is rasterized. Other
   * images aren’t rasterized unless implicitly created by this method. For example, if you create
   * a subset Image from a rasterized Image, the subset Image doesn’t create its own GPU cache but
   * uses the full resolution cache created by the original Image. If you want the subset Image to
   * create its own GPU cache, call makeRasterized() on the subset Image. The returned Image always
   * has the same mipmap state as the original Image.
   * @param rasterizationScale The factor to scale the Image by when rasterizing. The default value
   * is 1.0, indicating that the Image should be rasterized at its current size. If the value is
   * greater than 1.0, it may result in blurring.
   * @param sampling The sampling options to apply when rasterizing the Image if the
   * rasterizationScale is not 1.0.
   * @return If the Image is already rasterized and the rasterizationScale is 1.0, the original
   * Image is returned. If the rasterizationScale is less than zero, nullptr is returned.
   */
  virtual std::shared_ptr<Image> makeRasterized(float rasterizationScale = 1.0f,
                                                const SamplingOptions& sampling = {}) const;

  /**
   * Returns a filtered Image with the specified filter. The filter has the potential to alter the
   * bounds of the source Image. If the clipRect is not nullptr, the filtered Image will be clipped
   * accordingly. The offset will store the translation information to be applied when drawing the
   * filtered Image. If the filter is nullptr or fails to apply, nullptr is returned.
   */
  std::shared_ptr<Image> makeWithFilter(std::shared_ptr<ImageFilter> filter,
                                        Point* offset = nullptr,
                                        const Rect* clipRect = nullptr) const;

  /**
   * Returns an Image with the RGBAAA layout that takes half of the original Image as its RGB
   * channels and the other half as its alpha channel. If both alphaStartX and alphaStartY are zero,
   * a subset Image is returned. Returns nullptr if the original Image is alpha only, or the alpha
   * area is not fully contained by the original Image.
   * @param displayWidth The display width of the RGBAAA image.
   * @param displayHeight The display height of the RGBAAA image.
   * @param alphaStartX The x position of where alpha area begins in the original image.
   * @param alphaStartY The y position of where alpha area begins in the original image.
   */
  std::shared_ptr<Image> makeRGBAAA(int displayWidth, int displayHeight, int alphaStartX,
                                    int alphaStartY) const;

 protected:
  enum class Type {
    Buffer,
    Codec,
    Decoder,
    Filter,
    Generator,
    Mipmap,
    Orient,
    Picture,
    Rasterized,
    RGBAAA,
    Texture,
    Subset
  };

  virtual Type type() const = 0;

  std::weak_ptr<Image> weakThis;

  virtual std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware = true) const;

  virtual std::shared_ptr<Image> onMakeMipmapped(bool enabled) const = 0;

  virtual std::shared_ptr<Image> onMakeSubset(const Rect& subset) const;

  virtual std::shared_ptr<Image> onMakeOriented(Orientation orientation) const;

  virtual std::shared_ptr<Image> onMakeWithFilter(std::shared_ptr<ImageFilter> filter,
                                                  Point* offset, const Rect* clipRect) const;

  /**
   * Returns a texture proxy for the entire Image.
   * @param args The TPArgs used to create the texture proxy.
   */
  virtual std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const;

  /**
   * Returns a fragment processor for the entire Image.
   * @param args The FPArgs used to create the fragment processor.
   * @param tileModeX The tile mode applied in the x direction.
   * @param tileModeY The tile mode applied in the y direction.
   * @param sampling The sampling options used when sampling the Image.
   * @param uvMatrix The matrix used to transform the uv coordinates.
   */
  virtual std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                                 TileMode tileModeX,
                                                                 TileMode tileModeY,
                                                                 const SamplingOptions& sampling,
                                                                 const Matrix* uvMatrix) const = 0;

  friend class FragmentProcessor;
  friend class RuntimeImageFilter;
  friend class TransformImage;
  friend class RGBAAAImage;
  friend class RasterizedImage;
  friend class ImageShader;
  friend class Caster;
};
}  // namespace tgfx
