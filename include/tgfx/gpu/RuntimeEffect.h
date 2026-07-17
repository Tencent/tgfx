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
#include "tgfx/core/MapDirection.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

/**
 * RuntimeEffect supports creating custom ImageFilter objects using the shading language of the
 * current GPU backend.
 */
class RuntimeEffect {
 public:
  /**
   * Constructs a RuntimeEffect with the given extra input images.
   * @param extraInputs A collection of additional input images used during rendering. When the
   * onDraw() method is called, these extraInputs will be converted to inputTextures.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  explicit RuntimeEffect(const std::vector<std::shared_ptr<Image>>& extraInputs = {})
      : extraInputs(extraInputs) {
  }

  virtual ~RuntimeEffect() = default;

  /**
   * Returns the bounds of the image that will be produced by this filter when it is applied to an
   * image of the given bounds. MapDirection::Forward is used to determine which pixels of the
   * destination canvas a source image rect would touch after filtering. MapDirection::Reverse
   * is used to determine which rect of the source image would be required to fill the given
   * rect (typically, clip bounds).
   */
  virtual Rect filterBounds(const Rect& srcRect, MapDirection) const {
    return srcRect;
  }

 protected:
  /**
   * Applies the effect to the input textures and draws the result to the specified output texture.
   * inputTextures[0] represents the source image for the ImageFilter, and extraInputs correspond to
   * inputTextures[1...n] in order.
   */
  virtual bool onDraw(CommandEncoder* encoder,
                      const std::vector<std::shared_ptr<Texture>>& inputTextures,
                      std::shared_ptr<Texture> outputTexture, const Point& offset) const = 0;

 private:
  std::vector<std::shared_ptr<Image>> extraInputs = {};

  friend class RuntimeProgramCreator;
  friend class RuntimeDrawTask;
  friend class RuntimeImageFilter;
};
}  // namespace tgfx
