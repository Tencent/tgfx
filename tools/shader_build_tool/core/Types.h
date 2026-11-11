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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/shaders/contract/ShaderBundleContract.h"

namespace tgfx {

// The data structures that travel between the tool's stages. They previously lived in
// BundleWriter.h, which made every producer — the reflection extractor, the report writers, the
// contract rules — include the bundle writer's header just to name a type it fills in. Declaring
// them here gives them a home at the bottom of the tool (next to Diagnostics, see the sublayer
// map in LayeringTest) that any sublayer may depend on:
//
//   compile ─▶ (extracts) StageReflectionData ─▶ VariantData ─▶ (checks) verify
//                                        └──────────────▶ (writes) output
//
// A structure may move into this file only when at least two sublayers exchange it; a type used
// by one sublayer alone stays in that sublayer's own header.

struct UniformEntry {
  std::string name;
  uint8_t format = 0;      // UniformFormat enum value
  uint16_t arraySize = 1;  // std140 array element count; 1 when not an array
};

// One vertex-stage input of a kernel: the attribute slot the runtime vertex binding must match.
// The location is the single source of truth for how the GL/GLES backends bind attribute slots,
// so recording it in the bundle lets the loader verify the .vert's declared layout.
struct VertexInputEntry {
  std::string name;
  uint8_t format = 0;  // UniformFormat enum value
  uint8_t location = 0;
};

/// One uniform field the runtime may freeze at pipeline creation: name matches the UBO member and
/// the SPEC_<name>() call sites; firstId is where the generated constant ids start (element-major,
/// four ids per ivec4 element for arrays; id 0 is the master switch).
struct SpecEntry {
  std::string name;
  uint8_t format = 0;      // UniformFormat enum value
  uint16_t arraySize = 1;  // std140 array element count; 1 when not an array
  uint16_t firstId = 0;
};

/// One code gate: `#pragma tgfx_spec_gate <name> : <expr>` in a reader file, directly above a
/// function definition. The expression may reference the SPEC_<name>() scalar accessors, integer
/// literals and the OP_* constants, with comparison and boolean operators. A closed gate swaps
/// the function's body for a generated stub — never its signature — so every declaration
/// (uniforms, samplers, varyings) stays outside the gate and the layout cannot drift.
struct SpecGateInfo {
  std::string name;
  std::string expr;
  /// The gated function's name, parsed at scan time: the preprocessor drops unknown pragma
  /// lines, so the text passes locate the function by name, never by the pragma.
  std::string functionName;
};

struct StageReflectionData {
  std::vector<UniformEntry> uniforms;
  std::vector<UniformEntry> samplers;
  std::vector<VertexInputEntry> vertexInputs;
  /// The spec table (fragment stages only): the int/ivec4 fields the source reads through
  /// SPEC_<name>() accessors, so the runtime can freeze them at pipeline creation. Empty for
  /// stages whose source carries no specialization include point.
  std::vector<SpecEntry> specEntries;
};

struct VariantData {
  std::string shaderName;
  /// The variant's bundle id (VariantIdOf): what the runtime looks the entry up by. The compile
  /// step stamps it from the declaration table, so the writer never needs a family name.
  uint32_t variantId = 0;
  uint32_t vertPermutationIndex = 0;
  uint32_t fragPermutationIndex = 0;
  std::string profileTag;
  /// One of BundleFormat::Payload: what the two blobs hold. A profile's emitters all produce the
  /// same kind, and the writer states it once in the bundle header.
  uint16_t payloadKind = BundleFormat::Payload::Text;
  std::vector<uint8_t> vertexBlob;
  std::vector<uint8_t> fragmentBlob;
  StageReflectionData vertexReflection;
  StageReflectionData fragmentReflection;
};

}  // namespace tgfx
