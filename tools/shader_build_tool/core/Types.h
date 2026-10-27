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

struct StageReflectionData {
  std::vector<UniformEntry> uniforms;
  std::vector<UniformEntry> samplers;
  std::vector<VertexInputEntry> vertexInputs;
};

struct VariantData {
  std::string shaderName;
  uint32_t vertPermutationIndex = 0;
  uint32_t fragPermutationIndex = 0;
  std::string profileTag;
  std::vector<uint8_t> vertexBlob;
  std::vector<uint8_t> fragmentBlob;
  StageReflectionData vertexReflection;
  StageReflectionData fragmentReflection;
};

}  // namespace tgfx
