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
  size_t elementSize = 0;
  switch (_format) {
    case UniformFormat::Float:
      elementSize = sizeof(float);
      break;
    case UniformFormat::Float2:
      elementSize = 2 * sizeof(float);
      break;
    case UniformFormat::Float3:
      elementSize = 3 * sizeof(float);
      break;
    case UniformFormat::Float4:  // fall-through
    case UniformFormat::Float2x2:
      elementSize = 4 * sizeof(float);
      break;
    case UniformFormat::Float3x3:
      elementSize = 9 * sizeof(float);
      break;
    case UniformFormat::Float4x4:
      elementSize = 16 * sizeof(float);
      break;
    case UniformFormat::Int:
      elementSize = sizeof(int32_t);
      break;
    case UniformFormat::Int2:
      elementSize = 2 * sizeof(int32_t);
      break;
    case UniformFormat::Int3:
      elementSize = 3 * sizeof(int32_t);
      break;
    case UniformFormat::Int4:
      elementSize = 4 * sizeof(int32_t);
      break;
    case UniformFormat::Texture2DSampler:
    case UniformFormat::TextureExternalSampler:
    case UniformFormat::Texture2DRectSampler:
      elementSize = sizeof(int32_t);  // Samplers are represented as integers.
      break;
  }
  return elementSize * _count;
}
}  // namespace tgfx
