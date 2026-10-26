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

#include <optional>
#include <string_view>

namespace tgfx {

/**
 * The shader profile a precompiled bundle is built for: the dialect or binary format of its code
 * blobs. Several profiles can belong to one runtime Backend, for example the desktop and the ES
 * flavor of OpenGL, so a profile is not a Backend. shader_build_tool writes one bundle per profile
 * (--backends takes the tags below) and the runtime loads the bundle of the profile its context
 * runs (see ShaderProfileResolver.h).
 *
 * Layering. This is a contract header shared by shader_build_tool and the runtime. It holds the
 * enum and pure functions and includes nothing but the standard library. The mapping from a
 * runtime Backend to a profile needs runtime types, so it lives in ShaderProfileResolver.h:
 *
 *   ShaderProfile.h  (this file)  <------  ShaderProfileResolver.h  (runtime only)
 *        ^
 *        |
 *   shader_build_tool, PermutationCompilesForBackend, the bundle loader
 *
 * The tag strings are stored in every bundle header and are part of every stage key hash
 * (ShaderKeyHash.h), so they are fixed forever. ShaderProfileTest pins them.
 */
enum class ShaderProfile { OpenGL, OpenGLES, Vulkan, Metal, WebGPU };

/**
 * Every profile, in the order the tool reports them.
 */
inline constexpr ShaderProfile AllShaderProfiles[] = {
    ShaderProfile::OpenGL, ShaderProfile::OpenGLES, ShaderProfile::Vulkan, ShaderProfile::Metal,
    ShaderProfile::WebGPU};

/**
 * Returns the tag of the profile: the text stored in the bundle header and used for the bundle
 * file name (shader_bundle.<tag>.bin). Adding a profile makes the compiler flag every switch over
 * ShaderProfile that does not handle it, so none of them has a default case.
 */
inline const char* ProfileTag(ShaderProfile profile) {
  switch (profile) {
    case ShaderProfile::OpenGL:
      return "opengl";
    case ShaderProfile::OpenGLES:
      return "opengles";
    case ShaderProfile::Vulkan:
      return "vulkan";
    case ShaderProfile::Metal:
      return "metal";
    case ShaderProfile::WebGPU:
      return "webgpu";
  }
  return "";
}

/**
 * Returns the profile with the given tag, or nullopt when no profile has it. Parsing user input
 * with this fails closed: a misspelled tag is rejected instead of silently selecting nothing.
 */
inline std::optional<ShaderProfile> ParseProfileTag(std::string_view tag) {
  for (auto profile : AllShaderProfiles) {
    if (tag == ProfileTag(profile)) {
      return profile;
    }
  }
  return std::nullopt;
}

/**
 * Returns true when draws on this profile can read the destination color through framebuffer
 * fetch, which is what the variants with the framebuffer fetch dst kind (HAS_XP == 2) use.
 * The bundle of a profile without it does not contain those variants: the runtime can never
 * request them, and the closure verifier must not report them missing.
 *
 * Metal reads the destination through the subpassInput to [[color(0)]] translation. OpenGLES uses
 * the GL_EXT_shader_framebuffer_fetch dialect. WGSL cannot express the fetch at all. Desktop
 * OpenGL has no such path. The Vulkan backend never wires the chain: ShaderCaps leaves
 * frameBufferFetchSupport false there and the render pass has no input attachment binding, so a
 * draw always resolves a dst texture. The Vulkan variants are excluded, not deleted: the GLSL
 * sources and the matcher rules keep enumerating them, so supporting the fetch on Vulkan later
 * needs the caps flag, the render pass wiring and this function returning true.
 */
inline bool ProfileSupportsFramebufferFetch(ShaderProfile profile) {
  switch (profile) {
    case ShaderProfile::Metal:
    case ShaderProfile::OpenGLES:
      return true;
    case ShaderProfile::OpenGL:
    case ShaderProfile::Vulkan:
    case ShaderProfile::WebGPU:
      return false;
  }
  return false;
}

}  // namespace tgfx
