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

// shader_variant_dump — Enumerate all shader variants registered in the TGFX processor surface
// and emit a JSON inventory that lists every variant's preamble together with the GLSL module
// source code it references. Pure offline tool: no Context, GPU, or texture resources required.
//
// Usage:
//   shader_variant_dump [--output <path>]
//
// Default output path: shader_variants.json in the current working directory.

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "VariantDumper.h"

namespace {

void PrintUsage(const char* programName) {
  std::cerr << "Usage: " << programName << " [--output <path>]\n"
            << "  --output <path>   Write JSON to <path> (defaults to shader_variants.json).\n"
            << "  -                 Write JSON to stdout.\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string outputPath = "shader_variants.json";
  bool writeStdout = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--output") {
      if (i + 1 >= argc) {
        PrintUsage(argv[0]);
        return 2;
      }
      outputPath = argv[++i];
    } else if (arg == "-" || arg == "--stdout") {
      writeStdout = true;
    } else if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return 2;
    }
  }

  if (writeStdout) {
    tgfx::DumpAllVariants(std::cout);
    return 0;
  }

  std::ofstream file(outputPath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open output file: " << outputPath << "\n";
    return 1;
  }
  tgfx::DumpAllVariants(file);
  file.close();
  if (!file) {
    std::cerr << "Failed to write output file: " << outputPath << "\n";
    return 1;
  }
  std::cerr << "Wrote shader variant inventory to " << outputPath << "\n";
  return 0;
}