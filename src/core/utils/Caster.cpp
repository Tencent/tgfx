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

#include "Caster.h"
#include "core/shaders/ColorShader.h"

namespace tgfx {
const ColorShader* ShaderCaster::AsColorShader(const Shader* shader) {
  if (shader->type() == Shader::Type::Color) {
    return static_cast<const ColorShader*>(shader);
  }
  return nullptr;
}

const ImageShader* ShaderCaster::AsImageShader(const Shader* shader) {
  if (shader->type() == Shader::Type::Image) {
    return static_cast<const ImageShader*>(shader);
  }
  return nullptr;
}

const GradientShader* ShaderCaster::AsGradientShader(const Shader* shader) {
  if (shader->type() == Shader::Type::Gradient) {
    return static_cast<const GradientShader*>(shader);
  }
  return nullptr;
}

const BlurImageFilter* ImageFilterCaster::AsBlurImageFilter(const ImageFilter* imageFilter) {
  if (imageFilter->type() == ImageFilter::Type::Blur) {
    return static_cast<const BlurImageFilter*>(imageFilter);
  }
  return nullptr;
}
const DropShadowImageFilter* ImageFilterCaster::AsDropShadowImageFilter(
    const ImageFilter* imageFilter) {
  if (imageFilter->type() == ImageFilter::Type::DropShadow) {
    return static_cast<const DropShadowImageFilter*>(imageFilter);
  }
  return nullptr;
}

const InnerShadowImageFilter* ImageFilterCaster::AsInnerShadowImageFilter(
    const ImageFilter* imageFilter) {
  if (imageFilter->type() == ImageFilter::Type::InnerShadow) {
    return static_cast<const InnerShadowImageFilter*>(imageFilter);
  }
  return nullptr;
}

const ModeColorFilter* ColorFilterCaster::AsModeColorFilter(const ColorFilter* colorFilter) {
  if (colorFilter->type() == ColorFilter::Type::Blend) {
    return static_cast<const ModeColorFilter*>(colorFilter);
  }
  return nullptr;
}

const MatrixColorFilter* ColorFilterCaster::AsMatrixColorFilter(const ColorFilter* colorFilter) {
  if (colorFilter->type() == ColorFilter::Type::Matrix) {
    return static_cast<const MatrixColorFilter*>(colorFilter);
  }
  return nullptr;
}

const PictureImage* ImageCaster::AsPictureImage(const Image* image) {
  if (image->type() == Image::Type::Picture) {
    return static_cast<const PictureImage*>(image);
  }
  return nullptr;
}

const GeneratorImage* ImageCaster::AsGeneratorImage(const Image* image) {
  if (image->type() == Image::Type::Generator) {
    return static_cast<const GeneratorImage*>(image);
  }
  return nullptr;
}

const ShaderMaskFilter* MaskFilterCaster::AsShaderMaskFilter(const MaskFilter* maskFilter) {
  if (maskFilter->type() == MaskFilter::Type::Shader) {
    return static_cast<const ShaderMaskFilter*>(maskFilter);
  }
  return nullptr;
}

const ImageCodec* ImageGeneratorCaster::AsImageCodec(const ImageGenerator* imageGenerator) {
  if (imageGenerator->type() == ImageGenerator::Type::ImageCodec) {
    return static_cast<const ImageCodec*>(imageGenerator);
  }
  return nullptr;
}

}  // namespace tgfx