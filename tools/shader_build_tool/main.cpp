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

// The thin command line of the tool: parse the arguments, pin the compiler targets, and hand the
// run to the one command the tool has — writing the shader library bundle the runtime embeds.
//
//   main ─▶ cli (BuildCommand, FunctionCatalog, OperatorCodegen)
//             ├─▶ compile     the compiler primitives, the stage caches and the SPIR-V library
//             ├─▶ verify      the source-level checks the bundle build gates on
//             └─▶ core        the shared sources and diagnostics helpers
//
// main.cpp is the only file that knows argv exists; no command or sublayer ever sees it again.

#include <iostream>
#include <string>
#include "cli/BuildCommand.h"
#include "compile/ShaderCompiler.h"
#include "compile/StageCache.h"
#include "compile/StageCompiler.h"

int main(int argc, char** argv) {
  tgfx::BuildOptions options;
  if (!tgfx::ParseArgs(argc, argv, &options)) {
    // 2: the tool could not be invoked as asked (bad arguments). 1 stays "the product was
    // rejected": scripts can tell a usage mistake from a real build failure.
    return 2;
  }
  if (options.shaderDir.empty() || options.libraryBundlePath.empty()) {
    std::cerr << "ERROR: --shader-dir and --library-bundle are required\n";
    tgfx::PrintUsage();
    return 2;
  }
  std::string targetError;
  if (!tgfx::SetMetalTarget(options.metalSdk, options.metalMinOS, &targetError)) {
    std::cerr << "ERROR: " << targetError << "\n";
    return 2;
  }
  if (!tgfx::SetD3D12ShaderFormat(options.d3d12ShaderFormat, &targetError)) {
    std::cerr << "ERROR: " << targetError << "\n";
    return 2;
  }
  std::string bundleError;
  const bool withMSL = options.libraryLanguages.find("msl") != std::string::npos;
  const bool withWgsl = options.libraryLanguages.find("wgsl") != std::string::npos;
  const bool withSpirv = options.libraryLanguages.find("spirv") != std::string::npos;
  tgfx::StageCache stageCache;
  tgfx::StageCompiler stageCompiler(&stageCache);
  if (!tgfx::WriteShaderLibraryBundle(options.shaderDir, options.libraryBundlePath,
                                      (withMSL || withWgsl) ? &stageCompiler : nullptr, withWgsl,
                                      withSpirv, &bundleError)) {
    std::cerr << "ERROR: " << bundleError << "\n";
    return 1;
  }
  return 0;
}
