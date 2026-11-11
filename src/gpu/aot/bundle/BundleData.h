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

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "gpu/Uniform.h"
#include "gpu/aot/spec/SpecSource.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

// The in-memory shape of one parsed bundle. It lives beside the cache rather than inside it because
// the reader that fills it and the cache that stores it are separate translation units: a nested
// type would force the reader to be a member function of the cache again, which is what made the
// loader one 1000-line file.

/// One vertex-stage input recorded in the bundle: the attribute slot layout the stage declares. The
/// runtime binds attributes by location, so a mismatch with what the geometry processor emits is a
/// layout contract violation the loader can detect.
struct VertexInput {
  std::string name;
  UniformFormat format = UniformFormat::Float;
  uint32_t location = 0;
};

/// One stage's artifact and its reflection: the compiled payload plus everything the runtime needs
/// to bind it (uniforms, samplers, vertex inputs, and the freezable spec fields).
struct ShaderStageBlob {
  std::vector<uint8_t> data;
  std::vector<Uniform> uniforms;
  std::vector<Uniform> samplers;
  std::vector<VertexInput> vertexInputs;
  std::vector<SpecFieldInfo> specFields;
};

/// The immutable parse of one bundle. loadBundle() shares one instance per (identity hash, byte
/// size) across every cache in the process, so an embedded bundle is parsed once rather than once
/// per Context; the parse is contention-free read-only afterwards.
struct ParsedBundle {
  std::string profileTag;
  uint64_t identityHash = 0;
  uint32_t toolchainABI = 0;
  uint16_t payloadFormat = BundleFormat::Payload::Text;
  std::unordered_map<uint32_t, ShaderStageBlob> vertEntries;
  std::unordered_map<uint32_t, ShaderStageBlob> fragEntries;
};

}  // namespace tgfx
