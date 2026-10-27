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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>
#include "core/Types.h"

namespace tgfx {

/// Computes the inline expansion of an unoptimized SPIR-V module (functions not inlined yet, debug
/// names present, as the -O0 compile used for reflection produces). Returns an invalid result for
/// a malformed module or one without exactly one entry point. The analysis reads the SPIR-V words
/// directly — no third-party parser — so it stays a leaf of the compile layer. The policy over the
/// results lives in the verify layer (ValidateInlineExpansion) and the JSON report in the output
/// layer (WriteInlineExpansionReport); the records they exchange travel through core/Types.h.
InlineExpansion AnalyzeInlineExpansion(const std::vector<uint32_t>& spirv);

}  // namespace tgfx
