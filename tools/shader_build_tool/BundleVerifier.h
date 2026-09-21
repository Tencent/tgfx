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

#include <string>

namespace tgfx {

/// Verifies the shader bundles in a directory against the rule-reachable candidate set. For
/// every shader_bundle.<tag>.bin of a known backend present in the directory it checks: header
/// constants match the current writer contract (magic, format version, toolchain ABI), the
/// layout offsets are consistent, the stored identity hash recomputes, every pool entry resolves
/// to exactly one candidate stage (family, stage, permutation index) for that backend, and no
/// expected candidate is missing from the bundle. Returns the violation count (0 = clean) so the
/// process exit code can reject a mismatched or stale bundle.
int VerifyBundles(const std::string& bundleDir);

}  // namespace tgfx
