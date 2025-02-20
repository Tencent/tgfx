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
#include "Log.h"
#include "core/images/CodecImage.h"
#include "core/shaders/ColorShader.h"

namespace tgfx {
bool Caster::Compare(const Shader* shader, const Shader* other) {
  DEBUG_ASSERT(shader != nullptr);
  DEBUG_ASSERT(other != nullptr);
  return shader->isEqual(other);
}

const ColorShader* Caster::AsColorShader(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  if (shader->type() == Shader::Type::Color) {
    return static_cast<const ColorShader*>(shader);
  }
  return nullptr;
}

const ImageShader* Caster::AsImageShader(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  if (shader->type() == Shader::Type::Image) {
    return static_cast<const ImageShader*>(shader);
  }
  return nullptr;
}

const GradientShader* Caster::AsGradientShader(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  if (shader->type() == Shader::Type::Gradient) {
    return static_cast<const GradientShader*>(shader);
  }
  return nullptr;
}

const ColorFilterShader* Caster::AsColorFilterShader(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  if (shader->type() == Shader::Type::ColorFilter) {
    return static_cast<const ColorFilterShader*>(shader);
  }
  return nullptr;
}

const BlendShader* Caster::AsBlendShader(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  if (shader->type() == Shader::Type::Blend) {
    return static_cast<const BlendShader*>(shader);
  }
  return nullptr;
}

const MatrixShader* Caster::AsMatrixShader(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  if (shader->type() == Shader::Type::Matrix) {
    return static_cast<const MatrixShader*>(shader);
  }
  return nullptr;
}

const BlurImageFilter* Caster::AsBlurImageFilter(const ImageFilter* imageFilter) {
  DEBUG_ASSERT(imageFilter != nullptr);
  if (imageFilter->type() == ImageFilter::Type::Blur) {
    return static_cast<const BlurImageFilter*>(imageFilter);
  }
  return nullptr;
}
const DropShadowImageFilter* Caster::AsDropShadowImageFilter(const ImageFilter* imageFilter) {
  DEBUG_ASSERT(imageFilter != nullptr);
  if (imageFilter->type() == ImageFilter::Type::DropShadow) {
    return static_cast<const DropShadowImageFilter*>(imageFilter);
  }
  return nullptr;
}

const InnerShadowImageFilter* Caster::AsInnerShadowImageFilter(const ImageFilter* imageFilter) {
  DEBUG_ASSERT(imageFilter != nullptr);
  if (imageFilter->type() == ImageFilter::Type::InnerShadow) {
    return static_cast<const InnerShadowImageFilter*>(imageFilter);
  }
  return nullptr;
}

bool Caster::Compare(const ColorFilter* colorFilter, const ColorFilter* other) {
  DEBUG_ASSERT(colorFilter != nullptr);
  DEBUG_ASSERT(other != nullptr);
  return colorFilter->isEqual(other);
}

const ModeColorFilter* Caster::AsModeColorFilter(const ColorFilter* colorFilter) {
  DEBUG_ASSERT(colorFilter != nullptr);
  if (colorFilter->type() == ColorFilter::Type::Blend) {
    return static_cast<const ModeColorFilter*>(colorFilter);
  }
  return nullptr;
}

const MatrixColorFilter* Caster::AsMatrixColorFilter(const ColorFilter* colorFilter) {
  DEBUG_ASSERT(colorFilter != nullptr);
  if (colorFilter->type() == ColorFilter::Type::Matrix) {
    return static_cast<const MatrixColorFilter*>(colorFilter);
  }
  return nullptr;
}

const AlphaThresholdColorFilter* Caster::AsAlphaThresholdColorFilter(
    const ColorFilter* colorFilter) {
  DEBUG_ASSERT(colorFilter != nullptr);
  if (colorFilter->type() == ColorFilter::Type::AlphaThreshold) {
    return static_cast<const AlphaThresholdColorFilter*>(colorFilter);
  }
  return nullptr;
}

const ComposeColorFilter* Caster::AsComposeColorFilter(const ColorFilter* colorFilter) {
  DEBUG_ASSERT(colorFilter != nullptr);
  if (colorFilter->type() == ColorFilter::Type::Compose) {
    return static_cast<const ComposeColorFilter*>(colorFilter);
  }
  return nullptr;
}

const PictureImage* Caster::AsPictureImage(const Image* image) {
  DEBUG_ASSERT(image != nullptr);
  if (image->type() == Image::Type::Picture) {
    return static_cast<const PictureImage*>(image);
  }
  return nullptr;
}

const CodecImage* Caster::AsCodecImage(const Image* image) {
  DEBUG_ASSERT(image != nullptr);
  if (image->type() == Image::Type::Codec) {
    return static_cast<const CodecImage*>(image);
  }
  return nullptr;
}

const SubsetImage* Caster::AsSubsetImage(const Image* image) {
  DEBUG_ASSERT(image != nullptr);
  if (image->type() == Image::Type::Subset) {
    return static_cast<const SubsetImage*>(image);
  }
  return nullptr;
}

bool Caster::Compare(const MaskFilter* maskFilter, const MaskFilter* other) {
  DEBUG_ASSERT(maskFilter != nullptr);
  DEBUG_ASSERT(other != nullptr);
  return maskFilter->isEqual(other);
}

const ShaderMaskFilter* Caster::AsShaderMaskFilter(const MaskFilter* maskFilter) {
  DEBUG_ASSERT(maskFilter != nullptr);
  if (maskFilter->type() == MaskFilter::Type::Shader) {
    return static_cast<const ShaderMaskFilter*>(maskFilter);
  }
  return nullptr;
}

}  // namespace tgfx