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

#include "tgfx/gpu/Attribute.h"

namespace tgfx {
size_t Attribute::size() const {
  switch (_format) {
    case VertexFormat::Float:
      return sizeof(float);
    case VertexFormat::Float2:
      return 2 * sizeof(float);
    case VertexFormat::Float3:
      return 3 * sizeof(float);
    case VertexFormat::Float4:
      return 4 * sizeof(float);
    case VertexFormat::Half:
      return sizeof(uint16_t);
    case VertexFormat::Half2:
      return 2 * sizeof(uint16_t);
    case VertexFormat::Half3:
      return 3 * sizeof(uint16_t);
    case VertexFormat::Half4:
      return 4 * sizeof(uint16_t);
    case VertexFormat::Int:
      return sizeof(int32_t);
    case VertexFormat::Int2:
      return 2 * sizeof(int32_t);
    case VertexFormat::Int3:
      return 3 * sizeof(int32_t);
    case VertexFormat::Int4:
      return 4 * sizeof(int32_t);
    case VertexFormat::UByteNormalized:
      return sizeof(uint8_t);
    case VertexFormat::UByte2Normalized:
      return 2 * sizeof(uint8_t);
    case VertexFormat::UByte3Normalized:
      return 3 * sizeof(uint8_t);
    case VertexFormat::UByte4Normalized:
      return 4 * sizeof(uint8_t);
  }
  return 0;
}
}  // namespace tgfx
