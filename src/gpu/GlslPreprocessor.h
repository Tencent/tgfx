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

#include <string>

namespace tgfx {

/**
 * Preprocess OpenGL-style GLSL (any version: 150 / 300 es / 450 ...) to Vulkan-compatible GLSL
 * 450 with explicit binding / location qualifiers, ready to be handed to shaderc::Compiler or
 * glslang. The transform is pure text and performs:
 *
 *   1. Rewrite `#version <N>` -> `#version 450`.
 *   2. Assign binding=0 / binding=1 to the canonical VertexUniformBlock / FragmentUniformBlock.
 *   3. Assign sequential binding=N to any remaining custom std140 uniform blocks.
 *   4. Assign sequential binding=N to every `uniform samplerXxx name;` declaration.
 *   5. Add `layout(location=N)` to every `in` variable (preserving interpolation / precision
 *      qualifiers).
 *   6. Add `layout(location=N)` to every `out` variable (same preservation).
 *   7. Drop `precision <highp|mediump|lowp> <type>;` declarations (unsupported on desktop 450).
 *
 * Used by both the runtime Metal path (MetalShaderModule.mm) and the offline GLSL→HLSL tool
 * (tools/glsl_to_usf) — the two must stay bit-identical to guarantee parity between the Metal
 * path's GPU-verified shaders and the offline UE RHI output.
 */
std::string PreprocessGLSLForVulkan(const std::string& glslCode);

}  // namespace tgfx
