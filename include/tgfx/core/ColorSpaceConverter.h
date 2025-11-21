/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/Color.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/ImageInfo.h"

namespace tgfx {

/**
 * ImageData contains imageinfo and data for ColorSpaceConverter.
 */
struct ImageData {
  ImageInfo info = ImageInfo{};
  std::shared_ptr<Data> data = nullptr;
};

/**
 * ColorSpaceConverteris an abstract callback base class for the PDF and SVG exporters. Users can
 * inherit from this class to implement custom color and image conversion logic.
 */
class ColorSpaceConverter {
 public:
  /**
   * Create a default converter that performs no conversion.
   */
  static std::shared_ptr<ColorSpaceConverter> MakeDefaultConverter();

  /**
   * Pure virtual function for Color conversion.
   */
  virtual Color convertColor(const Color& color) const = 0;

  /**
   * Pure virtual function for image conversion.
   */
  virtual ImageData convertImage(const ImageData& imageData) const = 0;

  virtual ~ColorSpaceConverter() = default;
};
}  // namespace tgfx
