/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include "gpu/aot/spec/SpecSource.h"
#include "gpu/aot/spec/SpecValues.h"
#include "gpu/shaders/Family.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

class ProgramInfo;

/// The variant's specialization table, derived from its declaration: the same order, names and id
/// assignment the assembler declares them with, so the injector addresses the constants without
/// reading the bundle's table.
std::vector<SpecFieldInfo> DeriveSpecFieldInfos(const Variant& variant);

/// The one entry point for "can this draw specialize, and with which values". Both routes (the
/// plain processor-tree route and the pass-IR route) call it, so the enable switch, the format
/// gate and the capture live in one place: the two routes cannot drift into one specializing
/// while the other serves the generic form for the same variant.
class SpecResolver {
 public:
  /**
   * Resolves the specialization values of one draw.
   * @param source    The artifact store the draw's reflected layouts come from.
   * @param backend   The backend whose injector decides whether the values are consumable.
   * @param info      The draw's processor tree and pipeline state.
   * @param variant   The matched variant; its id selects the bundle's spec table.
   * @return          The values in spec-table order (the master switch first), or an empty set
   *                  when the draw serves the generic form.
   */
  static SpecValues Resolve(SpecSource* source, Backend backend, const ProgramInfo& info,
                            const Variant& variant);
};

/// The code format a backend's payload arrives in. The backend picks the dialect and the bundle
/// header says whether that dialect arrives as driver source text or as a precompiled container;
/// Metal and D3D12 support both forms, so their choice is read from the bundle rather than
/// assumed. Shared with the precompiled creator, whose module descriptors name the same format.
ShaderCodeFormat SpecFormatForBackend(Backend backend, uint16_t payloadFormat);

}  // namespace tgfx
