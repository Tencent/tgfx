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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include "gpu/SLType.h"
#include "gpu/Uniform.h"

namespace tgfx {

/// One std140 layout row: the footprint of a single element and the stride between array elements.
struct Std140Layout {
  uint32_t size = 0;         // The bytes one element occupies (a vec3 occupies 12).
  uint32_t alignment = 0;    // The base alignment (a vec3 aligns to 16).
  uint32_t arrayStride = 0;  // The bytes between array elements (a vec3 array strides 16).
};

/// The one layout table. A type without a block footprint (Void, Half, a sampler) returns a zero
/// row; callers treat size == 0 as "this type is not a uniform member".
Std140Layout Std140LayoutOf(UniformFormat format);
Std140Layout Std140LayoutOf(SLType type);

/// The SLType one uniform format stores; Void outside the known set.
SLType SLTypeOf(UniformFormat format);

/// Aligns a byte offset to the given base alignment (a no-op for alignment 0).
uint32_t Std140AlignUp(uint32_t offset, uint32_t alignment);

}  // namespace tgfx
