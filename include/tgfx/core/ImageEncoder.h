/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include <memory>
#include "tgfx/core/EncodedFormat.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

class Pixmap;
class WriteStream;

/**
 * Encodes a single PNG or JPEG from consecutive, full-width bands in top-to-bottom order.
 * The encoder does not retain the supplied pixels. Calls must be serialized.
 */
class ImageEncoder {
 public:
  /**
   * Accepts RGBA_8888 or BGRA_8888. JPEG requires opaque input; composite transparency
   * before encoding. A failed encode can leave partial data in the destination.
   */
  static std::unique_ptr<ImageEncoder> Make(std::shared_ptr<WriteStream> destination,
                                            const ImageInfo& imageInfo, EncodedFormat format,
                                            int quality = 100);

  ~ImageEncoder();
  ImageEncoder(const ImageEncoder&) = delete;
  ImageEncoder& operator=(const ImageEncoder&) = delete;

  /**
   * Appends rows with the same width, color type, alpha type and color space as imageInfo.
   * A band can have a different row stride from imageInfo.
   */
  bool appendRows(const Pixmap& band);

  /**
   * Finalizes the file. All rows must have been appended. Does not flush the destination.
   */
  bool finish();

 private:
  class Impl;
  explicit ImageEncoder(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl = nullptr;
};

}  // namespace tgfx
