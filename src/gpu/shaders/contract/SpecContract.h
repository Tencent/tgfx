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

namespace tgfx {

/**
 * The names of the generated specialization surface. The build tool emits them into the shader
 * text, the runtime injects values into them, and the tool's own scanners look for them — one
 * spelling for all three, because a rename on one side alone would silently stop the injection and
 * leave every draw serving the generic form.
 */
namespace SpecContract {

/// The per-value constant: `<prefix><id>` in the generated text, and the macro the runtime defines
/// (GLSL) or the function constant index (Metal) carries.
inline constexpr const char* ConstantPrefix = "TGFX_SPEC_";

/// The spec-constant spelling SPIRV-Cross's HLSL writer produces; the runtime's HLSL injector
/// defines these instead of ConstantPrefix.
inline constexpr const char* HlslConstantPrefix = "SPIRV_CROSS_CONSTANT_ID_";

/// The master switch's identifier (`ConstantPrefix` + 0): 1 means the draw is specialized.
inline constexpr const char* MasterSwitch = "TGFX_SPEC_0";

/// The macro that marks an executor whose call sites were fully unrolled at build time.
inline constexpr const char* UnrolledExecutorMacro = "TGFX_SPEC_UNROLLED_EXECUTOR";

/// The per-field accessor (`<prefix><field>`), which reads the constant when specialized and the
/// uniform otherwise.
inline constexpr const char* FieldAccessorPrefix = "SPEC_";

/// The master-switch accessor generated once per specialized shader.
inline constexpr const char* ActiveAccessor = "SPEC_ACTIVE";

/// The per-gate accessor (`<prefix><gate>`); the text-gate pass wraps its body.
inline constexpr const char* GateAccessorPrefix = "SPEC_GATE_";

/// The preprocessor macro a text gate defines; the runtime removes a closed gate's body by
/// defining the value constants before the driver's preprocessor runs.
inline constexpr const char* GateMacroPrefix = "TGFX_GATE_";

}  // namespace SpecContract

}  // namespace tgfx
