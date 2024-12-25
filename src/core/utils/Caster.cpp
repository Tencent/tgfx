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

namespace tgfx {
std::shared_ptr<const ColorShader> ShaderCaster::AsColorShader(
    const std::shared_ptr<Shader>& shader) {
  if (shader->type() == Shader::Type::Color) {
    return std::static_pointer_cast<const ColorShader>(shader);
  }
  return nullptr;
}

std::shared_ptr<const ImageShader> ShaderCaster::AsImageShader(
    const std::shared_ptr<Shader>& shader) {
  if (shader->type() == Shader::Type::Image) {
    return std::static_pointer_cast<const ImageShader>(shader);
  }
  return nullptr;
}

std::shared_ptr<const GradientShader> ShaderCaster::AsGradientShader(
    const std::shared_ptr<Shader>& shader) {
  if (shader->type() == Shader::Type::Gradient) {
    return std::static_pointer_cast<const GradientShader>(shader);
  }
  return nullptr;
}

std::shared_ptr<const BlurImageFilter> ImageFilterCaster::AsBlurImageFilter(
    const std::shared_ptr<ImageFilter>& imageFilter) {
  if (imageFilter->type() == ImageFilter::Type::Blur) {
    return std::static_pointer_cast<const BlurImageFilter>(imageFilter);
  }
  return nullptr;
}
std::shared_ptr<const DropShadowImageFilter> ImageFilterCaster::AsDropShadowImageFilter(
    const std::shared_ptr<ImageFilter>& imageFilter) {
  if (imageFilter->type() == ImageFilter::Type::DropShadow) {
    return std::static_pointer_cast<const DropShadowImageFilter>(imageFilter);
  }
  return nullptr;
}

std::shared_ptr<const InnerShadowImageFilter> ImageFilterCaster::AsInnerShadowImageFilter(
    const std::shared_ptr<ImageFilter>& imageFilter) {
  if (imageFilter->type() == ImageFilter::Type::InnerShadow) {
    return std::static_pointer_cast<const InnerShadowImageFilter>(imageFilter);
  }
  return nullptr;
}

std::shared_ptr<const ModeColorFilter> ColorFilterCaster::AsModeColorFilter(
    const std::shared_ptr<ColorFilter>& colorFilter) {
  if (colorFilter->type() == ColorFilter::Type::Blend) {
    return std::static_pointer_cast<const ModeColorFilter>(colorFilter);
  }
  return nullptr;
}

std::shared_ptr<const MatrixColorFilter> ColorFilterCaster::AsMatrixColorFilter(
    const std::shared_ptr<ColorFilter>& colorFilter) {
  if (colorFilter->type() == ColorFilter::Type::Matrix) {
    return std::static_pointer_cast<const MatrixColorFilter>(colorFilter);
  }
  return nullptr;
}

std::shared_ptr<const PictureImage> ImageCaster::AsPictureImage(
    const std::shared_ptr<Image>& image) {
  if (image->type() == Image::Type::Picture) {
    return std::static_pointer_cast<const PictureImage>(image);
  }
  return nullptr;
}

}  // namespace tgfx