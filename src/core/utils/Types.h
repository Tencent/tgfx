/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include <tgfx/layers/LayerContent.h>
#include "tgfx/core/ColorFilter.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/MaskFilter.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
class Types {
 public:
  using ShaderType = Shader::Type;
  using ImageFilterType = ImageFilter::Type;
  using ColorFilterType = ColorFilter::Type;
  using ImageType = Image::Type;
  using MaskFilterType = MaskFilter::Type;

  static ShaderType Get(const Shader* shader);
  static ImageFilterType Get(const ImageFilter* imageFilter);
  static ColorFilterType Get(const ColorFilter* colorFilter);
  static ImageType Get(const Image* image);
  static MaskFilterType Get(const MaskFilter* maskFilter);

  static bool Compare(const Shader* shader, const Shader* other);
  static bool Compare(const ColorFilter* colorFilter, const ColorFilter* other);
  static bool Compare(const MaskFilter* maskFilter, const MaskFilter* other);
};
}  // namespace tgfx
