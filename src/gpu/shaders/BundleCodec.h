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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/**
 * The reflection pool's serialized grammar, single-sourced: the tool's writer and the runtime's
 * loader both go through this file, so a grammar change is one edit instead of two that must be
 * kept in agreement. The byte layout is a format fact of the bundle (see ShaderBundleContract.h):
 * [uniformCount:u8][samplerCount:u8][vertexInputCount:u8][reserved:u8], then per uniform and per
 * sampler [nameLen:u8][name][format:u8][arraySize:u16 LE], then per vertex input
 * [nameLen:u8][name][format:u8][location:u8].
 *
 * The codec is byte-level only. The format byte is an opaque UniformFormat value (the tool links
 * no Uniform.h), so the semantic rules — which formats may be uniforms, which samplers, sampler
 * array sizes, and vertex-input locations that strictly increase — stay with the runtime, which
 * owns the enum.
 */

/// One uniform or sampler entry of a stage's reflection.
struct BundleReflectionEntry {
  std::string name;
  uint8_t format = 0;      ///< UniformFormat enum value, opaque here
  uint16_t arraySize = 1;  ///< std140 array element count; 1 when not an array
};

/// One vertex-stage input: the attribute slot the runtime vertex binding must match.
struct BundleVertexInput {
  std::string name;
  uint8_t format = 0;  ///< UniformFormat enum value, opaque here
  uint8_t location = 0;
};

/// One stage's reflection as the bundle stores it.
struct BundleReflection {
  std::vector<BundleReflectionEntry> uniforms;
  std::vector<BundleReflectionEntry> samplers;
  std::vector<BundleVertexInput> vertexInputs;
};

/// Serializes one stage's reflection into the pool's byte grammar.
std::vector<uint8_t> SerializeBundleReflection(const BundleReflection& reflection);

/**
 * Parses one stage's reflection from its pool bytes. Structural validation is included: names
 * are non-empty, NUL-free and unique within their list, and the input must not end early.
 * Returns false on any violation; *reflection is then partially filled and the caller discards
 * it.
 */
bool ParseBundleReflection(const uint8_t* data, size_t length, BundleReflection* reflection);

}  // namespace tgfx
