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

#include <optional>
#include <string>
#include <vector>
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

/// The parsed command line of a build run, shared by the two commands (build, verify) because
/// they share the shader directory and the Metal target options.
struct BuildOptions {
  std::string shaderDir;
  std::string outDir;
  std::vector<ShaderProfile> backends;
  bool compress = false;
  std::string stageReportPath;
  // Set by --dump-stages: the per-variant stage dump two builds are compared with.
  std::string stageDumpDir;
  std::string inlineReportPath;
  std::string verifyBundleDir;
  // Set by --require-backends; stays empty (nullopt) when the flag is absent.
  std::optional<std::vector<std::string>> requiredBackends;
  // Persistent stage cache. Defaults to <out-dir>/stage_cache; --no-cache keeps it memory-only.
  std::string cacheDir;
  bool noCache = false;
  // The platform the Metal bundle is compiled for (see SetMetalTarget).
  std::string metalSdk = "macosx";
  /// What a d3d12 bundle stores: dxbc (bytecode compiled here, the default) or hlsl (source the
  /// backend compiles). Requesting dxbc on a host without D3DCompiler fails the build rather than
  /// quietly producing the other form.
  std::string d3d12ShaderFormat = "dxbc";
  std::string metalMinOS;
};

/// Parses argv into options. Prints the usage and returns false on an unknown option or an
/// unknown backend tag.
bool ParseArgs(int argc, char** argv, BuildOptions* options);

/// Runs the build: loads the sources, plans and compiles every family's variants through the
/// emitters, runs the contract rules, and writes the bundles with their manifests and the
/// reports. Returns the process exit code.
int RunBuildCommand(const BuildOptions& options);

}  // namespace tgfx
