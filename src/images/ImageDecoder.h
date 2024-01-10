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

namespace tgfx {
/**
 * ImageDecoder is a task that decodes an image to ImageBuffer.
 */
class ImageDecoder {
 public:
  /*
   * Create an ImageDecoder wraps the existing ImageBuffer.
   */
  static std::shared_ptr<ImageDecoder> Wrap(std::shared_ptr<ImageBuffer> imageBuffer);

  /**
   * Create an ImageDecoder from the specified ImageGenerator. If asyncDecoding is true, the
   * returned ImageDecoder schedules an asynchronous image-decoding task immediately. Otherwise, the
   * image will be decoded synchronously when the decode() method is called.
   */
  static std::shared_ptr<ImageDecoder> MakeFrom(std::shared_ptr<ImageGenerator> generator,
                                                bool tryHardware = true, bool asyncDecoding = true);

  virtual ~ImageDecoder() = default;

  /**
   * Returns the width of the decoded image.
   */
  virtual int width() const = 0;

  /**
   * Returns the height of the decoded image.
   */
  virtual int height() const = 0;

  /**
   * Returns true if the decoded image represents transparency only.
   */
  virtual bool isAlphaOnly() const = 0;

  /**
   * Returns the decoded ImageBuffer.
   */
  virtual std::shared_ptr<ImageBuffer> decode() const = 0;
};
}  // namespace tgfx
