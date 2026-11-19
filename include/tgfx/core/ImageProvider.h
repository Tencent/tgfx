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

#include "tgfx/core/ColorSpace.h"
#include "tgfx/core/Image.h"

namespace tgfx {
/**
 * ImageProvider supplies image variants of different pixel densities for a single logical image.
 * It lets an image pick the variant that best matches the scale it is finally drawn at, so a layer
 * can show a higher resolution asset while being zoomed in without changing its layout size. Use
 * Image::MakeFrom(std::shared_ptr<ImageProvider>) to create an image from a provider.
 *
 * All variants must depict the same content with the same color space and alpha type; only the
 * pixel density may differ. The logical size returned by width() and height() defines the size used
 * for layout, bounds and hit testing, and must not depend on the requested scale.
 *
 * Variants may be added or replaced later. Create another Image from the provider and assign it
 * again, so that the layers holding the previous image redraw.
 */
class ImageProvider {
 public:
  virtual ~ImageProvider() = default;

  /**
   * Returns the logical width of the image in local units.
   */
  virtual int width() const = 0;

  /**
   * Returns the logical height of the image in local units.
   */
  virtual int height() const = 0;

  /**
   * Returns the variant that best matches the given scale, where the scale is the number of device
   * pixels per logical unit. A variant whose pixel size equals the logical size multiplied by the
   * scale has the right pixel density. If a higher-density variant is not available yet, return a
   * lower-density one.
   * Returns nullptr if no variant is available, in which case nothing is drawn.
   * This method is called during rendering, potentially several times per frame and from the
   * thread that issues the draw calls, so it should be fast and thread-safe.
   * @param scale The number of device pixels per logical unit for the current draw.
   */
  virtual std::shared_ptr<Image> getImage(float scale) const = 0;

  /**
   * Returns whether every variant is an alpha-only image. The default value is false.
   */
  virtual bool isAlphaOnly() const {
    return false;
  }

  /**
   * Returns the color space of every variant. The default value is sRGB.
   */
  virtual std::shared_ptr<ColorSpace> colorSpace() const {
    return ColorSpace::SRGB();
  }
};
}  // namespace tgfx
