//////////////////////////////////////////////////////////////////////////////////////////////////
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
//////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

namespace tgfx {

// Where the shader resources of one pipeline live, for the SPIR-V based backends (Vulkan, Metal,
// D3D12, WebGPU). The runtime pipelines create their descriptor sets or bind groups from these
// values, and the offline shader_build_tool reads the block names from the shaders it reflects.
//
// Layering. This is a contract header: constants only, nothing but the standard library. It lives
// apart from UniformData.h, which is a runtime class that pulls in Color, ColorSpace and Matrix,
// so the shader tool can share the layout without linking runtime types:
//
//   ShaderBindingLayout.h  (this file)
//        ^                      ^
//        |                      |
//   UniformData.h          shader_build_tool
//   (runtime pipelines)    (ReflectionExtractor, ...)
//
// Changing a value changes the resource layout every precompiled shader was built against, so it
// is also a bundle format change: bump ExpectedToolchainABI in PrecompiledBundleIdentity.h.

/// The name of the uniform block every vertex stage declares.
inline constexpr char VertexUniformBlockName[] = "VertexUniformBlock";

/// The name of the uniform block every fragment stage declares.
inline constexpr char FragmentUniformBlockName[] = "FragmentUniformBlock";

// Descriptor set indices for SPIR-V based backends. Vertex and fragment UBOs use independent
// sets so each stage can assign dense local bindings without colliding in Vulkan/WebGPU's
// pipeline-wide resource namespace. Textures use a third set.
inline constexpr int VERTEX_UBO_DESCRIPTOR_SET = 0;
inline constexpr int FRAGMENT_UBO_DESCRIPTOR_SET = 1;
inline constexpr int TEXTURE_DESCRIPTOR_SET = 2;

/// The binding of the vertex uniform block inside its descriptor set.
inline constexpr int VERTEX_UBO_BINDING_POINT = 0;

/// The binding of the fragment uniform block inside its descriptor set.
inline constexpr int FRAGMENT_UBO_BINDING_POINT = 1;

}  // namespace tgfx
