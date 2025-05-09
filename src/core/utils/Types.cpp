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

#include "Types.h"
#include "core/utils/Log.h"

namespace tgfx {

Types::ShaderType Types::Get(const Shader* shader) {
  DEBUG_ASSERT(shader != nullptr);
  return shader->type();
}

Types::ImageFilterType Types::Get(const ImageFilter* imageFilter) {
  DEBUG_ASSERT(imageFilter != nullptr)
  return imageFilter->type();
}

Types::ColorFilterType Types::Get(const ColorFilter* colorFilter) {
  DEBUG_ASSERT(colorFilter != nullptr)
  return colorFilter->type();
}

Types::ImageType Types::Get(const Image* image) {
  DEBUG_ASSERT(image != nullptr)
  return image->type();
}

Types::MaskFilterType Types::Get(const MaskFilter* maskFilter) {
  DEBUG_ASSERT(maskFilter != nullptr)
  return maskFilter->type();
}

bool Types::Compare(const Shader* shader, const Shader* other) {
  DEBUG_ASSERT(shader != nullptr);
  DEBUG_ASSERT(other != nullptr);
  return shader->isEqual(other);
}

bool Types::Compare(const ColorFilter* colorFilter, const ColorFilter* other) {
  DEBUG_ASSERT(colorFilter != nullptr);
  DEBUG_ASSERT(other != nullptr);
  return colorFilter->isEqual(other);
}

bool Types::Compare(const MaskFilter* maskFilter, const MaskFilter* other) {
  DEBUG_ASSERT(maskFilter != nullptr);
  DEBUG_ASSERT(other != nullptr);
  return maskFilter->isEqual(other);
}
}  // namespace tgfx
