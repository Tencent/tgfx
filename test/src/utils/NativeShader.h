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
//  License is distributed on an "AS is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/ShaderModule.h"
#include "tgfx/gpu/ShaderStage.h"

namespace tgfx {

/**
 * One shader stage expressed in every backend's native form, mirroring the public
 * ShaderCodeFormat contract: GLSL text for OpenGL, MSL text for Metal, SPIR-V for Vulkan, WGSL
 * text for WebGPU, HLSL text for D3D12. The native sources reproduce what the removed
 * GLSL-translation layer used to produce (including the per-backend clip-space y flip: Metal and
 * D3D12 negate y, GL/Vulkan/WebGPU pass it through), so pixel baselines stay identical.
 */
struct NativeShaderStage {
  /// OpenGL: GLSL body without a #version line (the helper prepends the right directive).
  const char* glsl = nullptr;
  /// Metal: complete MSL source with the entry function named `main0`.
  const char* msl = nullptr;
  /// Vulkan: precompiled SPIR-V words (entry `main`).
  const uint32_t* spirv = nullptr;
  /// Word count of `spirv`.
  size_t spirvLength = 0;
  /// WebGPU: complete WGSL source with the entry function named `main`.
  const char* wgsl = nullptr;
  /// D3D12: complete HLSL source with the entry function named `main`.
  const char* hlsl = nullptr;
  /// Uniform-block name to physical slot for the native formats (the GLSL path collects slots
  /// from the source itself and ignores this map): the `[[buffer(N)]]` index for MSL, the
  /// set/binding for SPIR-V, the `@binding(N)` index for WGSL, the `b{N}` register for HLSL.
  std::map<std::string, unsigned> uniformSlots;
};

/**
 * Fills `desc` for `stage` from the source that matches the GPU's backend. Returns false when
 * the stage has no source for the backend (the caller should skip the test); `desc` is only
 * valid when true is returned. Callers still fill `uniformSlots` themselves: the slot values are
 * per-effect contracts, not derivable here.
 */
bool FillNativeShaderDescriptor(GPU* gpu, ShaderStage stage, const NativeShaderStage& source,
                                ShaderModuleDescriptor* desc);

/**
 * Creates both shader modules of a two-stage pipeline. Returns false when the backend lacks a
 * source for either stage or module creation fails.
 */
bool CreateNativeShaderModules(GPU* gpu, const NativeShaderStage& vertexSource,
                               const NativeShaderStage& fragmentSource,
                               std::shared_ptr<ShaderModule>* vertexModule,
                               std::shared_ptr<ShaderModule>* fragmentModule);

}  // namespace tgfx
