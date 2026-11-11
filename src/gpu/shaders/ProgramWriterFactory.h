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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace tgfx {

class ProgramInfo;
class ProgramWriter;

/**
 * The writer one family's programs own: the chain family's fused writer, or the declared family's
 * layout-addressed writer. It lives with the families it names — the program creator and the
 * specialization capture both ask here, so the one place a family's writer is chosen is the
 * declaration layer rather than the artifact store. The info feeds the once-per-program checks
 * (the declared families' vertex pairing contract); the permutation ordinal lets a declared
 * family's writer derive its layout at construction instead of once per draw.
 * Returns null for a name outside the family table, which no matched variant can carry.
 */
std::unique_ptr<ProgramWriter> MakeProgramWriter(const ProgramInfo& info,
                                                 const std::string& shaderName,
                                                 uint32_t permutationOrdinal);

}  // namespace tgfx
