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

#include "core/DataSource.h"
#include "tgfx/core/ImageGenerator.h"

namespace tgfx {
/**
 * A DataSource that decodes an image and provides an ImageBuffer.
 */
class ImageSource : public DataSource<ImageBuffer> {
 public:
  /**
   * Create an image source from the specified ImageGenerator. If asyncDecoding is true, the
   * returned image source schedules an asynchronous image-decoding task immediately. Otherwise, the
   * image will be decoded synchronously when the getData() method is called.
   */
  static std::unique_ptr<DataSource> MakeFrom(std::shared_ptr<ImageGenerator> generator,
                                              bool tryHardware = true, bool asyncDecoding = true, std::shared_ptr<ColorSpace> colorSpace = nullptr);

  ImageSource(std::shared_ptr<ImageGenerator> generator, bool tryHardware, std::shared_ptr<ColorSpace> colorSpace = nullptr);

  std::shared_ptr<ImageBuffer> getData() const override {
    return generator->makeBuffer(tryHardware, colorSpace);
  }

 private:
  std::shared_ptr<ImageGenerator> generator = nullptr;
  bool tryHardware = true;
  std::shared_ptr<ColorSpace> colorSpace = nullptr;
};
}  // namespace tgfx
