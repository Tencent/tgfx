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
#include "tgfx/gpu/ShaderStage.h"

namespace shaderc {
class Compiler;
}

namespace tgfx {

/// Preprocesses OpenGL-style GLSL source code to Vulkan-compatible GLSL 450 with explicit
/// binding/location qualifiers. This includes upgrading the #version directive, assigning UBO and
/// sampler bindings, adding input/output location qualifiers, and removing precision declarations.
///
/// `stage` selects the semantics for interface variable location assignment so that varying
/// declarations line up across the vertex/fragment boundary regardless of source order:
///   - Vertex stage: `in` (vertex attributes) keep their source-declaration order — the assigned
///     locations must match the attribute order in `RenderPipelineDescriptor::vertex.bufferLayouts`,
///     which is the CPU-side contract every backend (Metal, Vulkan, D3D12) relies on. `out`
///     (varying) declarations are collected and sorted by name before receiving locations, so the
///     fragment side can pair them up without knowing the vertex side's source order.
///   - Fragment stage: `in` (varying) declarations are collected and sorted by name using the
///     same rule as the vertex `out` side, guaranteeing matching locations across stages. `out`
///     (colour attachments) keep their source-declaration order to preserve the mapping to the
///     colour attachment index.
std::string PreprocessGLSL(const std::string& glslCode, ShaderStage stage);

/// Compiles preprocessed GLSL 450 source to SPIR-V binary using shaderc. Returns an empty vector
/// on failure.
///
/// `preserveInterfaceVariables` controls the optimisation level:
///   - false (default): runs `shaderc_optimization_level_performance`, which is what Vulkan and
///     Metal want — both bind interface variables by name/location, so dead-stripping unused
///     fragment inputs is harmless and yields better generated code.
///   - true: runs `shaderc_optimization_level_zero` so every declared vertex output / fragment
///     input survives. D3D12 needs this because the SPIR-V → HLSL pass turns SPIR-V locations
///     into TEXCOORDn semantics; if the optimiser drops a fragment input, the resulting HLSL
///     mismatches the vertex shader's output signature and PSO creation fails.
std::vector<uint32_t> CompileGLSLToSPIRV(const shaderc::Compiler* compiler,
                                         const std::string& vulkanGLSL, ShaderStage stage,
                                         bool preserveInterfaceVariables = false);

}  // namespace tgfx
