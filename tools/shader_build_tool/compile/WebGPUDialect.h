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

#include <string>

namespace tgfx {

/**
 * Rewrites an expanded GLSL fragment or vertex source into the form the WGSL reader expects:
 * combined image samplers split into separate texture and sampler declarations with sequential
 * bindings, texture() lookups rewritten to the split form, and sampler-typed call arguments
 * expanded at every call site. Pure text: no SPIR-V, no tint — kept apart from the shaderc,
 * SPIRV-Cross and platform-toolchain code in ShaderCompiler.cpp, which is a different job.
 */
std::string RewriteSamplerLayoutForWGSL(const std::string& source);

}  // namespace tgfx
