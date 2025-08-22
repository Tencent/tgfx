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

#include "SLType.h"
#include <cinttypes>

namespace tgfx {
size_t GetSLTypeSize(SLType type) {
  switch (type) {
    case SLType::Float:
      return sizeof(float);
    case SLType::Float2:
      return 2 * sizeof(float);
    case SLType::Float3:
      return 3 * sizeof(float);
    case SLType::Float4:
      return 4 * sizeof(float);
    case SLType::Int:
      return sizeof(int32_t);
    case SLType::Int2:
      return 2 * sizeof(int32_t);
    case SLType::Int3:
      return 3 * sizeof(int32_t);
    case SLType::Int4:
      return 4 * sizeof(int32_t);
    case SLType::UByte4Color:
      return 4 * sizeof(uint8_t);
    case SLType::Float2x2:
      return 4 * sizeof(float);
    case SLType::Float3x3:
      return 9 * sizeof(float);
    case SLType::Float4x4:
      return 16 * sizeof(float);
    case SLType::Texture2DSampler:
    case SLType::Texture2DRectSampler:
    case SLType::TextureExternalSampler:
      return sizeof(int32_t);
    default:
      return 0;
  }
}
}  // namespace tgfx