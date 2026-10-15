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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/// One function of a shader stage, as seen from the stage's entry point.
struct InlinedFunction {
  /// The source-level function name (the GLSL name, without the parameter mangling).
  std::string name;
  /// Instructions in the function's own body, callees excluded.
  size_t instructions = 0;
  /// Copies of the body the driver ends up compiling: GPU compilers inline every call site, so a
  /// function called from k sites of a function that itself has n copies contributes k * n.
  size_t copies = 0;
};

/// The inline expansion of one shader stage.
struct InlineExpansion {
  bool valid = false;
  std::vector<InlinedFunction> functions;
  /// Instructions of every reachable function counted once.
  size_t ownInstructions = 0;
  /// Instructions after every call site is inlined.
  size_t expandedInstructions = 0;
};

/// Computes the inline expansion of an unoptimized SPIR-V module (functions not inlined yet, debug
/// names present, as the -O0 compile used for reflection produces). Returns an invalid result for
/// a malformed module or one without exactly one entry point.
InlineExpansion AnalyzeInlineExpansion(const std::vector<uint32_t>& spirv);

/// One stage analysis together with the variant it belongs to.
struct InlineExpansionRecord {
  std::string shaderName;
  uint32_t fragPermutationIndex = 0;
  InlineExpansion expansion;
};

/// Validates the expansion policy over the fragment stages: the instructions a function adds by
/// being inlined more than once (its own instructions times its copies beyond the first) must
/// stay below the limit in InlineExpansion.cpp, with no exceptions. A function with several call
/// sites is the pattern that multiplied the pointwise kernels' compile time (per-leaf tiled
/// fetches, per-mode HSL blend helpers, per-channel transfer functions, the XferProcessor blend
/// next to the loop's blend); funnelling the call sites into one is what removes the copies.
/// Returns the violation count and prints each violation.
size_t ValidateInlineExpansion(const std::vector<InlineExpansionRecord>& records);

/// Writes the per-variant expansion and every duplicated function to a JSON file. Returns false
/// when the file cannot be written.
bool WriteInlineExpansionReport(const std::string& path,
                                const std::vector<InlineExpansionRecord>& records);

}  // namespace tgfx
