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

#include <algorithm>
#include <string>
#include <utility>
#include "core/Diagnostics.h"
#include "core/Types.h"

namespace tgfx {
namespace contract_check {

// The helpers every contract check shares. They live here rather than in one check's translation
// unit so the checks can be split by what they read (compiled variants vs shader source) without
// one file having to own another's support code.

/// Reports one violation of a contract check. Every check records through this helper, so the
/// code, the subject and the severity stay in one shape for the cli renderer and the tests.
inline void Report(Diagnostics* diagnostics, Severity severity, const char* code,
                   const std::string& shader, const std::string& profile, int vert, int frag,
                   std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = code;
  diagnostic.shader = shader;
  diagnostic.profile = profile;
  diagnostic.vertPermutationIndex = vert;
  diagnostic.fragPermutationIndex = frag;
  diagnostic.message = std::move(message);
  diagnostics->report(std::move(diagnostic));
}

inline bool HasUniform(const StageReflectionData& reflection, const std::string& name) {
  return std::any_of(reflection.uniforms.begin(), reflection.uniforms.end(),
                     [&name](const UniformEntry& entry) { return entry.name == name; });
}

}  // namespace contract_check
}  // namespace tgfx
