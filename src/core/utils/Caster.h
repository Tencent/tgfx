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
#include "core/filters/ShaderMaskFilter.h"
#include "core/images/CodecImage.h"
#include "core/images/GeneratorImage.h"
#include "core/images/PictureImage.h"
#include "core/images/SubsetImage.h"
#include "core/shaders/ColorShader.h"
#include "core/shaders/GradientShader.h"
#include "core/shaders/ImageShader.h"
#include "tgfx/core/ImageCodec.h"
#include "tgfx/core/MaskFilter.h"

#pragma once

namespace tgfx {
/**
 * The Caster class provides static methods to cast objects to their derived types.
 */
class Caster {
 public:
  static const ColorShader* AsColorShader(const Shader* shader);

  static const ImageShader* AsImageShader(const Shader* shader);

  static const GradientShader* AsGradientShader(const Shader* shader);

  static const BlurImageFilter* AsBlurImageFilter(const ImageFilter* imageFilter);

  static const DropShadowImageFilter* AsDropShadowImageFilter(const ImageFilter* imageFilter);

  static const InnerShadowImageFilter* AsInnerShadowImageFilter(const ImageFilter* imageFilter);

  static const ModeColorFilter* AsModeColorFilter(const ColorFilter* colorFilter);

  static const MatrixColorFilter* AsMatrixColorFilter(const ColorFilter* colorFilter);

  static const PictureImage* AsPictureImage(const Image* image);

  static const CodecImage* AsCodecImage(const Image* image);

  static const SubsetImage* AsSubsetImage(const Image* image);

  static const ShaderMaskFilter* AsShaderMaskFilter(const MaskFilter* maskFilter);
};

}  // namespace tgfx