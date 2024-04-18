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

#include <array>
#include <memory>
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Color.h"

namespace tgfx {
class FragmentProcessor;

/**
 * ColorFilter is the base class for filters that perform color transformations in the drawing
 * pipeline.
 */
class ColorFilter {
 public:
  /**
   * Creates a new ColorFilter that transforms the input color into its corresponding brightness.
   */
  static std::shared_ptr<ColorFilter> Luma();

  /**
   * Creates a new ColorFilter that applies blends between the constant color (src) and input color
   * (dst) based on the BlendMode.
   */
  static std::shared_ptr<ColorFilter> Blend(Color color, BlendMode mode);

  /**
   * Creates a new ColorFilter that transforms the color using the given matrix.
   */
  static std::shared_ptr<ColorFilter> Matrix(const std::array<float, 20>& rowMajor);

  virtual ~ColorFilter() = default;

  /**
   * Returns true if the filter is guaranteed to never change the alpha of a color it filters.
   */
  virtual bool isAlphaUnchanged() const {
    return false;
  }

 private:
  virtual std::unique_ptr<FragmentProcessor> asFragmentProcessor() const = 0;

  friend class RenderContext;
  friend class ColorFilterShader;
};
}  // namespace tgfx
