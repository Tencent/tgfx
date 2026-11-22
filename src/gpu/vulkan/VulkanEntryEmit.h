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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/Uniform.h"
#include "gpu/VertexShaderBuilder.h"
#include "gpu/aot/chain/ChainProgramPlan.h"
#include "gpu/shaders/Layout.h"

namespace tgfx {

/**
 * Emits the fragment stage's frame module: the entry point the driver runs. It declares the
 * variant's varyings at their locations, declares the linked chainBody as an import, and its main
 * loads the varyings, calls chainBody with the fragment coordinate, and stores the result. The
 * parameter list follows the layout exactly, so the body emitted from the same plan links in.
 * @param plan     The planned program (its varyings must match the layout's).
 * @param layout   The program's derived interface.
 * @param out      Receives the module words.
 * @param error    Receives the failure reason.
 */
bool EmitVulkanFragmentFrame(const ChainProgramPlan& plan, const Layout& layout,
                             std::vector<uint32_t>* out, std::string* error);

/**
 * Emits the vertex stage from the recorded writes: the attributes at their locations, the vertex
 * uniform block at the declared members' std140 offsets, every recorded transform and store
 * replayed, and the clip-space position written last. A varying the records never produce makes
 * the emission fail instead of leaving the fragment stage reading an undefined value.
 * @param calls     The recorded writes, in emission order.
 * @param layout    The program's derived interface (attributes, varyings and their locations).
 * @param uniforms  The vertex stage's declared uniform members, in declaration order.
 * @param out       Receives the module words.
 * @param error     Receives the failure reason.
 */
bool EmitVulkanVertexBody(const std::vector<VertexCallRecord>& calls, const Layout& layout,
                          const std::vector<Uniform>& uniforms, std::vector<uint32_t>* out,
                          std::string* error);

}  // namespace tgfx
