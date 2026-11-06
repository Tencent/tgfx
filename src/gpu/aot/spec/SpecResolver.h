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
#include <string>
#include "gpu/aot/spec/SpecValues.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

class Context;
class ProgramInfo;

/// The one entry point for "can this draw specialize, and with which values". Both routes (the
/// plain processor-tree route and the pass-IR route) call it, so the enable switch, the format
/// gate and the capture live in one place: the two routes cannot drift into one specializing
/// while the other serves the generic form for the same variant.
class SpecResolver {
 public:
  /**
   * Resolves the specialization values of one draw.
   * @param context     The context whose precompiled cache holds the bundle.
   * @param info        The draw's processor tree and pipeline state.
   * @param shaderName  The variant's family name.
   * @param vertIndex   The variant's vertex permutation index.
   * @param fragIndex   The variant's fragment permutation index.
   * @return            The values in spec-table order (the master switch first), or an empty set
   *                    when the draw serves the generic form.
   */
  static SpecValues Resolve(Context* context, const ProgramInfo& info,
                            const std::string& shaderName, uint32_t vertIndex, uint32_t fragIndex);
};

/// The code format a backend's payload arrives in. The backend picks the dialect and the bundle
/// header says whether that dialect arrives as driver source text or as a precompiled container;
/// Metal and D3D12 support both forms, so their choice is read from the bundle rather than
/// assumed. Shared with the precompiled creator, whose module descriptors name the same format.
ShaderCodeFormat SpecFormatForBackend(Backend backend, uint16_t payloadFormat);

}  // namespace tgfx
