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

// The thin command line of the tool: parse the arguments, pin the Metal target, and hand the
// run to one of the three commands. Everything the commands do lives in the sublayers:
//
//   main ─▶ cli (BuildCommand, VerifyCommand, AuditCommand, ReportWriter)
//             ├─▶ plan        the variants a build must emit
//             ├─▶ compile     the compiler primitives and the stage caches
//             ├─▶ emit        the per-backend storage forms
//             ├─▶ verify      the contract rule registries
//             └─▶ core        the shared diagnostics
//
// main.cpp is the only file that knows argv exists; no command or sublayer ever sees it again.

#include <iostream>
#include <string>
#include "ShaderCompiler.h"
#include "cli/AuditCommand.h"
#include "cli/BuildCommand.h"
#include "cli/VerifyCommand.h"

int main(int argc, char** argv) {
  tgfx::BuildOptions options;
  if (!tgfx::ParseArgs(argc, argv, &options)) {
    // 2: the tool could not be invoked as asked (bad arguments). 1 stays "the product was
    // rejected": scripts can tell a usage mistake from a real build failure.
    return 2;
  }
  std::string targetError;
  if (!tgfx::SetMetalTarget(options.metalSdk, options.metalMinOS, &targetError)) {
    std::cerr << "ERROR: " << targetError << "\n";
    return 2;
  }
  if (options.audit) {
    return tgfx::RunAuditCommand();
  }
  if (!options.verifyBundleDir.empty()) {
    return tgfx::RunVerifyCommand(options);
  }
  return tgfx::RunBuildCommand(options);
}
