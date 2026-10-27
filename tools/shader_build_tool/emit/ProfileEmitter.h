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
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "ShaderCompiler.h"
#include "compile/StageCompiler.h"
#include "gpu/ShaderProfile.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

// The emit sublayer turns one compiled variant into the storage form of one backend. Each
// backend has one emitter, produced by MakeProfileEmitter, and the build loop asks them all in
// turn — the loop knows the variant and the emitters know their dialect:
//
//   plan (main.cpp) ─▶ common SPIR-V + reflection ─▶ every ProfileEmitter ─▶ bundle entries
//                                |
//                          StageCompiler (compile sublayer)
//
// An emitter prints why a variant failed and returns nullopt; the loop counts the failure, so
// the diagnostics stay in one place and the emitters stay free of build state.

/// One variant's two emitted blobs: exactly what one bundle entry stores.
struct EmittedStages {
  std::vector<uint8_t> vertexBlob;
  std::vector<uint8_t> fragmentBlob;
};

/// Everything an emitter consumes to emit one variant. The pointees belong to the caller and
/// stay alive for the duration of the call: the variant's identity and domains, the two template
/// sources with their define lists, the unoptimized vertex SPIR-V, and the unoptimized fragment
/// stage (spirv plus its compile outcome).
struct EmitInput {
  const PrecompiledShaderInfo* info = nullptr;
  uint32_t vertIndex = 0;
  uint32_t fragIndex = 0;
  const std::string* vertSource = nullptr;
  const std::string* fragSource = nullptr;
  const std::vector<std::string>* vertDefines = nullptr;
  const std::vector<std::string>* fragDefines = nullptr;
  const std::vector<uint32_t>* vertSpirv = nullptr;
  const SpirvStage* fragStage = nullptr;
};

/// Translates one variant into the storage form of one backend.
class ProfileEmitter {
 public:
  virtual ~ProfileEmitter() = default;

  /// The profile this emitter produces storage for.
  virtual ShaderProfile profile() const = 0;

  /// Emits both stages of the variant. On failure prints what failed to stderr and returns
  /// nullopt: the caller counts the failure and moves on to the next variant.
  virtual std::optional<EmittedStages> emit(const EmitInput& input, StageCompiler* compiler) = 0;
};

/// The one emitter of each profile. The switch has no default clause, so adding a profile
/// value without writing its emitter fails to compile this file instead of silently emitting
/// nothing.
std::unique_ptr<ProfileEmitter> MakeProfileEmitter(ShaderProfile profile);

}  // namespace tgfx
