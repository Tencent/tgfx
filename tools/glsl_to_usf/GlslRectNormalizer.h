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
#include <vector>
#include "GlslToSpv.h"

namespace tgfx {

/**
 * Metadata about one rect sampler that was rewritten to sampler2D. Consumed by the manifest
 * writer so the UE RHI side knows which SRV must receive a companion inverse-size uniform.
 */
struct RectSamplerInfo {
  std::string name;            // original uniform identifier, e.g. "TextureSampler_0_P1"
  std::string invSizeUniform;  // injected companion, e.g. "TGFX_InvRectSize_TextureSampler_0_P1"
};

/**
 * Result of the rect-to-2D normalization pass. When `errorMessage` is non-empty the rewrite
 * refused to proceed (structural mismatch); `glsl` is then left in an unspecified state and must
 * not be forwarded to shaderc.
 */
struct RectNormalizeResult {
  std::string glsl;
  std::vector<RectSamplerInfo> rects;
  std::string errorMessage;
};

/**
 * Rewrite a dumped GLSL source so every `sampler2DRect` uniform becomes a `sampler2D` uniform
 * paired with an injected `uniform vec2 TGFX_InvRectSize_<name>;`. Every sampling site whose
 * sampler arg is one of those uniforms has its coord arg multiplied by the companion
 * inverse-size uniform. Dead `sampler2DRect` overload bodies are stripped. Vertex stage is a
 * pass-through since none of the observed TGFX dumps declare rect uniforms in VS.
 *
 * The transform is deterministic and textual: no shaderc, no SPIR-V. On structural surprises
 * (e.g. a `#define` that references a rect uniform in an unrecognized shape) the function
 * returns an `errorMessage` rather than silently passing the shader through, so the runtime
 * emitter cannot introduce new patterns without this tool noticing.
 */
RectNormalizeResult NormalizeRectSamplers(const std::string& glsl, GlslStage stage);

}  // namespace tgfx
