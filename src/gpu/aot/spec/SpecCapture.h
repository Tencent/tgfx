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
#include "gpu/aot/spec/SpecSource.h"
#include "gpu/aot/spec/SpecValues.h"

namespace tgfx {

class ProgramInfo;

/**
 * Captures one draw's specialization values: the structural uniform values the precompiled
 * variant's spec table names, read back from a CPU scratch buffer the regular uniform writers
 * filled. There is no second encoding path — what the specialized shader folds is exactly what
 * the upload writes — and the values are deterministic (the scratch is zeroed before every
 * capture, and the planner's encoding is deterministic for one effect shape).
 *
 * The capture runs before any program lookup, so it must not touch the GPU: it borrows the
 * bundle's reflected layouts (never a linked program's) and the same ProgramWriter the draw will
 * use at submit time. Returns an empty SpecValues — meaning "serve the generic form" — whenever
 * specialization is disabled, the variant carries no spec table, the writer cannot produce the
 * draw's uniform content yet, or a spec field reads back out of range.
 */
class SpecCapture {
 public:
  /**
   * Captures the values of the draw `info` describes for the precompiled variant named by
   * (shaderName, vertIndex, fragIndex).
   * @param source      The artifact store the draw's reflected layouts come from.
   * @param info        The draw's processor tree and pipeline state.
   * @param shaderName  The variant's family name.
   * @param vertIndex   The variant's vertex permutation index.
   * @param fragIndex   The variant's fragment permutation index.
   * @return            The values in spec-table order (the master switch first), or empty when
   *                    the generic form should serve the draw.
   */
  static SpecValues Capture(SpecSource* source, const ProgramInfo& info,
                            const std::string& shaderName, uint32_t vertIndex, uint32_t fragIndex);
};

}  // namespace tgfx
