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

#include "VerifyCommand.h"
#include <iostream>
#include "cli/ReportWriter.h"
#include "core/Diagnostics.h"
#include "verify/BundleVerifier.h"

namespace tgfx {

int RunVerifyCommand(const BuildOptions& options) {
  // The verifier reports every violation as a diagnostic and counts what it covered; this
  // command renders both. It is the only printer in the verify path.
  Diagnostics diagnostics;
  BundleVerifyCounts counts;
  auto status = VerifyBundles(options.verifyBundleDir, options.shaderDir, options.requiredBackends,
                              &diagnostics, &counts);
  PrintDiagnostics(diagnostics);
  auto violations = diagnostics.count(Severity::Error) + diagnostics.count(Severity::Unverifiable);
  std::cout << "[verify] summary: " << counts.openedBundles << " bundle(s), " << violations
            << " violation(s)\n";
  if (!counts.requiredGiven && counts.openedBundles > 0) {
    // Without a required set a missing bundle is invisible to the exit code: say so, so a clean
    // result is not read as "every backend is covered".
    std::cout << "[verify] note: --require-backends not given; " << counts.absentNotRequired
              << " absent backend(s) were not treated as violations\n";
  }
  return status;
}

}  // namespace tgfx
