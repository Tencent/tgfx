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

#include <string>
#include <utility>
#include <vector>
#include "gpu/ShaderProfile.h"

namespace tgfx {

// Where the shader resources of one pipeline live, for the SPIR-V based backends (Vulkan, Metal,
// D3D12, WebGPU). The runtime pipelines create their descriptor sets or bind groups from these
// values, and the offline shader_build_tool reads the block names from the shaders it reflects.
//
// Layering. This is a contract header: constants and pure functions, nothing but the standard
// library and the other contract headers. It lives apart from UniformData.h, which is a runtime
// class that pulls in Color, ColorSpace and Matrix, so the shader tool can share the layout
// without linking runtime types:
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

// Descriptor set indices for SPIR-V based backends. The runtime pipelines build exactly three
// descriptor sets / bind groups — one per uniform block and one for the textures — and the
// precompiled shaders must declare their resources in the same three sets: vertex UBO in set 0,
// fragment UBO in set 1, textures in set 2. (The GLSL templates get these values injected as the
// TGFX_* contract macros below, so the numbers exist only in this file.)
inline constexpr int VERTEX_UBO_DESCRIPTOR_SET = 0;
inline constexpr int FRAGMENT_UBO_DESCRIPTOR_SET = 1;
inline constexpr int TEXTURE_DESCRIPTOR_SET = 2;

// The framebuffer-fetch subpass input of the FBF variants (HAS_XP == 2). Those variants only
// enter the Metal and GLES bundles — Metal's translation turns the subpass input into a
// [[color(N)]] read and the ES emitter deletes the declaration, so no stored form carries the
// set. Vulkan is excluded from the FBF variants today (ProfileSupportsFramebufferFetch); if it
// ever gains them, this must move off the texture set's bindings.
inline constexpr int FBF_INPUT_ATTACHMENT_SET = 3;

/// The binding of the vertex uniform block inside its descriptor set.
inline constexpr int VERTEX_UBO_BINDING_POINT = 0;

/// The binding of the fragment uniform block inside its descriptor set.
inline constexpr int FRAGMENT_UBO_BINDING_POINT = 1;

// Where the WebGPU rewrite packs the separated texture/sampler pairs of the texture set: pair i
// lives on bindings START + 2i (texture) and START + 2i + 1 (sampler). The runtime JIT rewrite
// and the render pipeline's bind-group expansion use the same packing.
inline constexpr int WEBGPU_TEXTURE_BINDING_START = 0;

/// The bindings of the separated texture/sampler pair i of one profile's texture set. WebGPU
/// packs the pairs interleaved from WEBGPU_TEXTURE_BINDING_START; the combined-sampler profiles
/// (GLSL, Vulkan SPIR-V, MSL) keep the template's declaration-order bindings, which reflection
/// reports dense from zero.
inline std::pair<int, int> SamplerBinding(ShaderProfile profile, int i) {
  switch (profile) {
    case ShaderProfile::WebGPU:
      return {WEBGPU_TEXTURE_BINDING_START + 2 * i, WEBGPU_TEXTURE_BINDING_START + 2 * i + 1};
    case ShaderProfile::OpenGL:
    case ShaderProfile::OpenGLES:
    case ShaderProfile::Vulkan:
    case ShaderProfile::Metal:
      return {i, i};
  }
  return {i, i};
}

/// The contract macros injected into every compiled variant, as "NAME=value" strings: the GLSL
/// templates spell their layout qualifiers with these names, so the numbers above exist in the
/// sources only through this list. shader_build_tool prepends it to every stage's defines, and
/// the manifests' source digest covers it, so a value change rebuilds and invalidates bundles.
inline std::vector<std::string> BindingContractDefines() {
  return {"TGFX_VERTEX_UBO_SET=" + std::to_string(VERTEX_UBO_DESCRIPTOR_SET),
          "TGFX_FRAGMENT_UBO_SET=" + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET),
          "TGFX_TEXTURE_SET=" + std::to_string(TEXTURE_DESCRIPTOR_SET),
          "TGFX_VERTEX_UBO_BINDING=" + std::to_string(VERTEX_UBO_BINDING_POINT),
          "TGFX_FRAGMENT_UBO_BINDING=" + std::to_string(FRAGMENT_UBO_BINDING_POINT),
          "TGFX_FBF_INPUT_SET=" + std::to_string(FBF_INPUT_ATTACHMENT_SET)};
}

}  // namespace tgfx
