/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
/**
 * Types of shader-language-specific boxed variables we can create.
 */
enum class SLType {
  Void,
  Half,
  Half2,
  Half3,
  Half4,
  Float,
  Float2,
  Float3,
  Float4,
  Float2x2,
  Float3x3,
  Float4x4,
  Short,
  Short2,
  Short3,
  Short4,
  UShort,
  UShort2,
  UShort3,
  UShort4,
  Int,
  Int2,
  Int3,
  Int4,
  UByte4Color,
  Texture2DSampler,
  TextureExternalSampler,
  Texture2DRectSampler,
};
}  // namespace tgfx
