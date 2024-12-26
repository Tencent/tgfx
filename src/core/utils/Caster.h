/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <memory>
#include "core/filters/BlurImageFilter.h"
#include "core/filters/DropShadowImageFilter.h"
#include "core/filters/InnerShadowImageFilter.h"
#include "core/filters/MatrixColorFilter.h"
#include "core/filters/ModeColorFilter.h"
#include "core/shaders/ColorShader.h"
#include "core/shaders/GradientShader.h"
#include "core/shaders/ImageShader.h"

#pragma once

namespace tgfx {

class ShaderCaster {
 public:
  static std::shared_ptr<const ColorShader> AsColorShader(const std::shared_ptr<Shader>& shader);

  static std::shared_ptr<const ImageShader> AsImageShader(const std::shared_ptr<Shader>& shader);

  static std::shared_ptr<const GradientShader> AsGradientShader(
      const std::shared_ptr<Shader>& shader);
};

class ImageFilterCaster {
 public:
  static std::shared_ptr<const BlurImageFilter> AsBlurImageFilter(
      const std::shared_ptr<ImageFilter>& imageFilter);

  static std::shared_ptr<const DropShadowImageFilter> AsDropShadowImageFilter(
      const std::shared_ptr<ImageFilter>& imageFilter);

  static std::shared_ptr<const InnerShadowImageFilter> AsInnerShadowImageFilter(
      const std::shared_ptr<ImageFilter>& imageFilter);
};

class ColorFilterCaster {
 public:
  static std::shared_ptr<const ModeColorFilter> AsModeColorFilter(
      const std::shared_ptr<ColorFilter>& colorFilter);

  static std::shared_ptr<const MatrixColorFilter> AsMatrixColorFilter(
      const std::shared_ptr<ColorFilter>& colorFilter);
};

}  // namespace tgfx