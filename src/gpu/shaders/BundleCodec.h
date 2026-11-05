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
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

/**
 * The reflection pool's serialized grammar, single-sourced: the tool's writer and the runtime's
 * loader both go through this file, so a grammar change is one edit instead of two that must be
 * kept in agreement. The byte layout is a format fact of the bundle (see ShaderBundleContract.h):
 * [uniformCount:u8][samplerCount:u8][vertexInputCount:u8][specCount:u8], then per uniform and per
 * sampler [nameLen:u8][name][format:u8][arraySize:u16 LE], then per vertex input
 * [nameLen:u8][name][format:u8][location:u8], then per spec entry
 * [nameLen:u8][name][format:u8][arraySize:u16 LE][firstId:u16 LE].
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

/// One uniform field the runtime may freeze at pipeline creation (the spec table). firstId is
/// where the constant ids start; an array consumes four ids per element, element-major, and id 0
/// is the master switch (never referenced by an entry).
struct BundleSpecEntry {
  std::string name;
  uint8_t format = 0;      ///< UniformFormat enum value, opaque here
  uint16_t arraySize = 1;  ///< std140 array element count; 1 when not an array
  uint16_t firstId = 0;
};

/// One stage's reflection as the bundle stores it.
struct BundleReflection {
  std::vector<BundleReflectionEntry> uniforms;
  std::vector<BundleReflectionEntry> samplers;
  std::vector<BundleVertexInput> vertexInputs;
  std::vector<BundleSpecEntry> specEntries;
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

/**
 * The bundle file header as every reader sees it. The field layout is BundleFormat's (contract
 * header); this struct is the one extraction of it, shared by the runtime loader, the offline
 * verifier and the manifest writer. Byte order is little-endian, as the format specifies.
 */
struct BundleHeader {
  uint32_t magic = 0;
  uint16_t formatVersion = 0;
  uint16_t compressionType = 0;
  uint64_t identityHash = 0;
  uint32_t toolchainABI = 0;
  uint32_t vertPoolCount = 0;
  uint32_t fragPoolCount = 0;
  uint32_t vertPoolOffset = 0;
  uint32_t fragPoolOffset = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  uint32_t reflectionOffset = 0;
  std::string profileTag;
  /// What the data pool holds, one of BundleFormat::Payload.
  uint16_t payloadFormat = 0;
};

/**
 * Extracts the header fields. Callers check the size before parsing (the format's own minimum is
 * BundleFormat::HeaderSize); this returns false for a null buffer or a short one so the check can
 * also live here.
 */
bool ParseBundleHeader(const uint8_t* data, size_t size, BundleHeader* header);

}  // namespace tgfx
