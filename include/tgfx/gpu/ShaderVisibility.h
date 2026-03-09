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

namespace tgfx {
/**
 * ShaderVisibility defines bitmask flags that specify which shader stages can access a resource
 * binding. These flags can be combined with bitwise OR to indicate visibility across multiple
 * stages, following the same pattern as WebGPU's GPUShaderStageFlags, Vulkan's
 * VkShaderStageFlags, and D3D12's D3D12_SHADER_VISIBILITY.
 */
namespace ShaderVisibility {
/**
 * The resource is accessible to the vertex shader stage.
 */
static constexpr uint32_t Vertex = 1 << 0;

/**
 * The resource is accessible to the fragment shader stage.
 */
static constexpr uint32_t Fragment = 1 << 1;

/**
 * The resource is accessible to both vertex and fragment shader stages.
 */
static constexpr uint32_t VertexFragment = Vertex | Fragment;
}  // namespace ShaderVisibility
}  // namespace tgfx
