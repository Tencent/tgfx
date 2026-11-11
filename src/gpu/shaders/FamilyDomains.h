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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Family.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/**
 * The permutation-domain view of a declaration-driven family: the free dims in flattened module
 * order (geometry first, body modules, Xfer last), which is exactly the radix order the variant
 * ordinal encodes. Both the build tool (bundle emission) and the runtime registry (the
 * post-match buildable check and the FBF profile filter) read this one conversion, so the
 * ordinal, the bundle keys and the domain can never disagree.
 */
PrecompiledShaderInfo AssembledFamilyInfo(const Family& family);

/// The legacy interpreter's dimension cell a declared Chain variant stands for. One decode shared
/// by the runtime matcher, the tool's equivalence test and the route assertions, so the mapping
/// between the legacy permutation space and the declared dims cannot fork.
struct ChainLegacyCell {
  int gpLayout = 0;
  int coverage = 1;
  int uvCoord = 0;
  int textureCount = 0;
  int mask = 0;
  int resource = 0;
  int subset = 0;
  int xp = 0;
};

/// Fills `cell` for a variant of the Chain family; false when the geometry cannot place a cell
/// (the family's allowed filter makes that unreachable, so it only guards misuse).
bool DecodeChainCell(const Family& family, const Variant& variant, ChainLegacyCell* cell);

/// The declared geometry index and PROFILE value a legacy cell encodes (the matcher's direction).
uint8_t ChainGeometryOfCell(const ChainLegacyCell& cell);
uint8_t ChainProfileOfCell(const ChainLegacyCell& cell);

/// The variant's identity in a bundle: Variant::id (the family index in the high bits, the
/// family-local ordinal in the low 20). The tool writes the bundle and the runtime reads it, and
/// both derive the id from the declaration table, so an id names the same variant on both sides
/// without a name hash. Returns UINT32_MAX beyond the id's field widths.
uint32_t VariantIdOf(uint16_t familyIndex, uint32_t ordinal);

}  // namespace tgfx
