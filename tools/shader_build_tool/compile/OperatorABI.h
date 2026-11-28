/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>

namespace tgfx {

/// The ABI type of one operator parameter, as the compiled SPIR-V module declares it. The
/// signature reflection and the manifest comparison both speak this vocabulary, so a module whose
/// lowered ABI drift from the .inc signature fails the bundle build instead of the runtime link.
enum class OperatorABIType : uint8_t {
  Float,
  Int,
  Vec2,
  Vec3,
  Vec4,
  Mat3,
  Mat4,
  IVec4,
  Vec4Array8,
  SampledImage,
  /// The type has no compact ABI spelling; a caller that meets it must reject the parameter.
  Unknown,
};

}  // namespace tgfx
