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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/Types.h"
#include "core/Diagnostics.h"

namespace tgfx {

/// The per-family outcome of a build: what was enumerated, what compiled, what failed.
struct ShaderReport {
  std::string name;
  uint32_t rawCount = 0;
  uint32_t compiledCount = 0;
  uint32_t errorCount = 0;
};

/// The whole build's outcome: the per-family reports, the produced variants, the inline
/// expansion analysis of every compiled fragment permutation, and the per-profile error ledger.
struct BuildReport {
  std::vector<ShaderReport> shaders;
  std::vector<VariantData> variants;
  // One per compiled fragment permutation, from its unoptimized SPIR-V.
  std::vector<InlineExpansionRecord> inlineExpansions;
  // The per-profile error ledger; the compile loop records through it and the report reads it.
  Diagnostics diagnostics;
  bool hasErrors = false;
  std::string errorMessage;
};

/// Writes the build report as <outDir>/shader_build_report.json: the per-shader counts and
/// stage bytes, and the per-profile artifact statistics (logical vs unique stages). Returns
/// false when the file cannot be written.
bool WriteReportJson(const BuildReport& report, const std::string& outDir);

/// Renders the recorded diagnostics to stderr, one block per record:
///   [E][verify] verify.clip.missing-field  shader=X frag=7
///     carries the clip contract but is missing uniform 'HasClip'
/// Records without a message are skipped (the compile loop still prints its own context until
/// its call sites report with messages).
void PrintDiagnostics(const Diagnostics& diagnostics);

}  // namespace tgfx
