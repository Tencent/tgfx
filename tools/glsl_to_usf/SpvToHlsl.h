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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GlslToSpv.h"

namespace tgfx {

/**
 * A (register type, register index) pair describing how SPIRV-Cross mapped a SPIR-V resource
 * (UBO, SRV, Sampler) to an HLSL register. Populated by ConvertSpvToHlsl and propagated into the
 * manifest file so that UE can bind parameters without re-parsing the HLSL output.
 */
struct HlslResourceBinding {
  std::string name;      // Name as it appears in the HLSL source (typically the GLSL name).
  std::string stage;     // "vertex" or "fragment".
  std::string regType;   // "b" (CBV), "t" (SRV), "s" (Sampler).
  uint32_t regIndex = 0;
};

/**
 * Mapping from SPIR-V location to HLSL vertex input semantic (ATTRIBUTEN). The tool always emits
 * ATTRIBUTEN to let UE's input layout handle the final slot assignment.
 */
struct HlslAttributeRemap {
  uint32_t location = 0;
  std::string semantic;  // e.g. "ATTRIBUTE0"
};

/**
 * Output of SpvToHlsl: the generated HLSL source plus the mapping tables the tool needs to
 * document in the manifest. `hlsl` is empty when `errorMessage` is non-empty.
 */
struct SpvToHlslResult {
  std::string hlsl;
  std::vector<HlslResourceBinding> cbvBindings;
  std::vector<HlslResourceBinding> srvBindings;
  std::vector<HlslResourceBinding> samplerBindings;
  std::vector<HlslAttributeRemap> attributes;  // only populated for vertex stage
  std::string errorMessage;
};

/**
 * Translate a SPIR-V binary (from CompileGlslToSpv) to HLSL source targeted at Shader Model 5.1.
 * Fills the manifest-friendly binding tables as well.
 *
 * `vertexAttributeCount`: when `stage == Vertex`, the tool assigns semantics ATTRIBUTE0..
 * ATTRIBUTE(N-1) to the first N input locations (ignored for Fragment).
 *
 * The entry point is named `MainVS` for vertex shaders and `MainPS` for fragment shaders, matching
 * UE's conventional global shader entry names. Resource bindings are assigned in the order the
 * SPIR-V reflection reports them: UBOs get b-registers starting at 0, sampled images are split
 * into SRV (t0..) and Sampler (s0..) pairs.
 */
SpvToHlslResult ConvertSpvToHlsl(const std::vector<uint32_t>& spirv, GlslStage stage,
                                 uint32_t vertexAttributeCount = 0);

}  // namespace tgfx
