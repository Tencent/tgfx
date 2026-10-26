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
#include "gpu/ShaderProfile.h"
#include "tgfx/gpu/Backend.h"

namespace tgfx {

// Maps the runtime to the shader profile of the bundle it consumes. This is the runtime side of
// ShaderProfile.h and depends on the Backend type, so shader_build_tool does not include it.
//
// The mapping from a Backend to a profile is not one to one for OpenGL. The profile of a GL
// context is a property of the context rather than of the backend enum, so it is resolved from
// whether the context runs the ES standard (GLES devices, WebGL, SwiftShader).

/**
 * Returns the profile a context of the backend consumes, or nullopt when the backend has no
 * precompiled shader support. OpenGL returns the desktop profile; whether an ES context also
 * accepts the OpenGLES bundle is decided by the loader through ResolveGLShaderProfile.
 */
inline std::optional<ShaderProfile> ExpectedShaderProfile(Backend backend) {
  switch (backend) {
    case Backend::OpenGL:
      return ShaderProfile::OpenGL;
    case Backend::Metal:
      return ShaderProfile::Metal;
    case Backend::Vulkan:
      return ShaderProfile::Vulkan;
    case Backend::WebGPU:
      return ShaderProfile::WebGPU;
    case Backend::Unknown:
    case Backend::D3D12:
      return std::nullopt;
  }
  return std::nullopt;
}

/**
 * Returns the profile of an OpenGL context. ShaderCaps assigns precision modifiers to every
 * GLES-family context (GLES, WebGL, SwiftShader), and desktop GL is the remaining case.
 */
inline ShaderProfile ResolveGLShaderProfile(bool usesPrecisionModifiers) {
  return usesPrecisionModifiers ? ShaderProfile::OpenGLES : ShaderProfile::OpenGL;
}

}  // namespace tgfx
