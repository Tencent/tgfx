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

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/**
 * Resolves the build tool's text gates in one stored driver source with this draw's spec values,
 * returning the text the driver actually parses: a closed gate's body is spliced out (its stub
 * stays) and the gate directives go, so the compiled text carries only the code this draw can
 * reach. The gates are tool-generated and flat — the stored-text form check rejects every other
 * conditional — so this is one line-oriented pass, not a parser.
 *
 * `valuesById` is indexed by the specialization id the emitted define line carries (id 0 is the
 * master switch), and `valuePrefix` is the format's value macro prefix (TGFX_SPEC_ or the
 * SPIRV-Cross HLSL spelling). Any surprise — an unterminated gate, a directive inside one, a
 * condition the canonical form does not spell — returns the input unchanged: the caller then
 * serves exactly what it served before this pass existed.
 */
std::string ResolveSpecGates(const std::string& text, const std::vector<uint32_t>& valuesById,
                             const char* valuePrefix);

/// Deletes `if` blocks whose branch condition this draw never reaches. The tool marks them after
/// preprocessing with `#pragma tgfx_spec_branch <id> <mask>` (a source pragma would be rewritten by
/// the preprocessor); the block stays when `valuesById[id] & mask` is non-zero and is removed whole
/// otherwise. The pragma itself never reaches the driver. An id the draw does not carry keeps its
/// block rather than deleting code the runtime cannot justify.
std::string ResolveSpecBranches(const std::string& text, const std::vector<uint32_t>& valuesById);

/// Deletes function definitions the text never calls, cascading until no definition is left
/// uncalled. The gates' stubs orphan whole reader/helper families whose call sites lived inside
/// the closed branches; the driver still parses and lowers every one of them, which measured 21%
/// of a chain program's cold compile (7.95 -> 6.25ms on a real GL driver) and 16% slower when
/// dead code was added back. `main` is never a candidate. Any definition whose braces do not
/// balance aborts the pass and returns the input unchanged.
std::string RemoveUncalledFunctions(const std::string& text);

}  // namespace tgfx
