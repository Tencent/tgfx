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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/AOTEffect.h"
#include "gpu/UniformData.h"

namespace tgfx {

/**
 * Writes one pointwise-operator slot's uniform record into the per-slot arrays of a multi-slot
 * precompiled kernel (PerlinNoiseFillShader, PointwiseTailShader). Array-element writes tolerate
 * targets that lack the arrays (runtime-generated programs), where the operator is baked into the
 * emitted code instead. slotCapacity is the kernel's array capacity (2 for the tail, 3 for
 * perlin).
 */
void UploadAOTPointwiseSlot(UniformData* uniformData, size_t slotIndex, uint32_t slotCapacity,
                            const AOTPointwiseSlot& slot);

}  // namespace tgfx
