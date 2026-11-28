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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>

namespace tgfx {

/// The single source of binding and naming constants for the shader pipeline. The shader build
/// tool and the runtime both include this header, so the two can never drift apart. The header is
/// deliberately self-contained: plain constants only, no runtime types. The names live in the
/// tgfx namespace directly (their original home) so consumers keep spelling them unqualified.

/// The name of the uniform block every vertex stage declares.
inline constexpr char VertexUniformBlockName[] = "VertexUniformBlock";

/// The name of the uniform block every fragment stage declares.
inline constexpr char FragmentUniformBlockName[] = "FragmentUniformBlock";

// Descriptor set indices. The runtime pipelines build exactly three descriptor sets / bind groups
// (one per uniform block plus one for the textures), so every shader form must declare its
// resources in the same three sets: vertex UBO in set 0, fragment UBO in set 1, textures in set 2.
// Changing a value changes the resource layout every compiled shader was built against, so it is
// a bundle format change too: bump the bundle version when that happens.
inline constexpr int VERTEX_UBO_DESCRIPTOR_SET = 0;
inline constexpr int FRAGMENT_UBO_DESCRIPTOR_SET = 1;
inline constexpr int TEXTURE_DESCRIPTOR_SET = 2;

// The framebuffer-fetch subpass input of the framebuffer-fetch variants. Those variants only
// enter the Metal and GLES forms; Vulkan is excluded from them, so the value never collides with
// the texture set's bindings.
inline constexpr int FBF_INPUT_ATTACHMENT_SET = 3;

/// The binding of the vertex uniform block inside its descriptor set.
inline constexpr int VERTEX_UBO_BINDING_POINT = 0;

/// The binding of the fragment uniform block inside its descriptor set.
inline constexpr int FRAGMENT_UBO_BINDING_POINT = 1;

/// The starting binding index of the WebGPU sampler packing: textures and their paired samplers
/// are numbered from this index in pairs. The tool's rewrite and the runtime path must agree.
inline constexpr int WEBGPU_TEXTURE_BINDING_START = 0;

// The bindings the shader build tool's dummy main uses for its stand-in samplers. The dummy entry
// (and these declarations) are stripped before linking; the numbers only have to avoid the real
// sampler range a compiling module could carry.
inline constexpr int DUMMY_RECT_SAMPLER_BINDING = 14;
inline constexpr int DUMMY_LEAF_SAMPLER_BINDING = 15;

/// The names of the dummy samplers the tool's wrapper main declares; the export patch removes the
/// declarations with the entry.
inline constexpr char DUMMY_RECT_SAMPLER_NAME[] = "dummyRectTex";
inline constexpr char DUMMY_LEAF_SAMPLER_NAME[] = "dummyLeafTex";

namespace SamplerContract {

// The prefix of every ordinary texture sampler: TextureSampler_<N>, with N dense from zero in
// declaration order. shader_build_tool checks the reflected samplers against this pattern.
constexpr const char* TextureSamplerPrefix = "TextureSampler_";

// Every semantic sampler name a shader may declare. Ordinary texture bindings use the generic
// TextureSampler_<N> pattern (N dense from zero, checked by shader_build_tool); the names below
// carry dedicated meaning and appear verbatim in the shaders, the runtime emitters, and the
// uniform writers. A new semantic sampler must be registered here or the bundle build fails — the
// point is that adding a binding interface is a deliberate contract change, not a typo.
constexpr const char* DstTexture = "DstTextureSampler";
constexpr const char* MaskTexture = "MaskTextureSampler";
constexpr const char* Mask = "MaskSampler";
constexpr const char* LocalMask = "LocalMaskSampler";
constexpr const char* EdgeMask = "EdgeMaskSampler";
constexpr const char* GradientTexture = "GradientTexture";

inline bool IsSemanticSamplerName(const std::string& name) {
  return name == DstTexture || name == MaskTexture || name == Mask || name == LocalMask ||
         name == EdgeMask || name == GradientTexture;
}

}  // namespace SamplerContract

}  // namespace tgfx
