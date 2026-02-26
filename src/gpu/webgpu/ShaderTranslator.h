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
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

/**
 * Translates GLSL shader code to WGSL for WebGPU.
 * @param glslCode The GLSL shader code to translate.
 * @param stage The shader stage (Vertex or Fragment).
 * @return The translated WGSL shader code, or an empty string if translation fails.
 */
std::string TranslateGLSLToWGSL(const std::string& glslCode, ShaderStage stage);

}  // namespace tgfx
