/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "Uniform.h"

namespace tgfx {
size_t Uniform::size() const {
  switch (_format) {
    case UniformFormat::Float:
      return sizeof(float);
    case UniformFormat::Float2:
      return 2 * sizeof(float);
    case UniformFormat::Float3:
      return 3 * sizeof(float);
    case UniformFormat::Float4:  // fall-through
    case UniformFormat::Float2x2:
      return 4 * sizeof(float);
    case UniformFormat::Float3x3:
      return 9 * sizeof(float);
    case UniformFormat::Float4x4:
      return 16 * sizeof(float);
    case UniformFormat::Int:
      return sizeof(int32_t);
    case UniformFormat::Int2:
      return 2 * sizeof(int32_t);
    case UniformFormat::Int3:
      return 3 * sizeof(int32_t);
    case UniformFormat::Int4:
      return 4 * sizeof(int32_t);
    case UniformFormat::Texture2DSampler:
    case UniformFormat::TextureExternalSampler:
    case UniformFormat::Texture2DRectSampler:
      return sizeof(int32_t);  // Samplers are represented as integers.
  }
  return 0;
}

#if DEBUG
const char* ToUniformFormatName(UniformFormat format) {
  switch (format) {
    case UniformFormat::Float:
      return "Float";
    case UniformFormat::Float2:
      return "Float2";
    case UniformFormat::Float3:
      return "Float3";
    case UniformFormat::Float4:
      return "Float4";
    case UniformFormat::Float2x2:
      return "Float2x2";
    case UniformFormat::Float3x3:
      return "Float3x3";
    case UniformFormat::Float4x4:
      return "Float4x4";
    case UniformFormat::Int:
      return "Int";
    case UniformFormat::Int2:
      return "Int2";
    case UniformFormat::Int3:
      return "Int3";
    case UniformFormat::Int4:
      return "Int4";
    case UniformFormat::Texture2DSampler:
      return "Texture2DSampler";
    case UniformFormat::TextureExternalSampler:
      return "TextureExternalSampler";
    case UniformFormat::Texture2DRectSampler:
      return "Texture2DRectSampler";
    default:
      return "?";
  }
}
#endif
}  // namespace tgfx
