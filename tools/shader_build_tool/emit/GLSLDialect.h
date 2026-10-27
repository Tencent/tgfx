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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include "ShaderCompiler.h"

namespace tgfx {

/// Emits the desktop GL 330 spelling of one stage: runs the standard GLSL preprocessor (the one
/// the SPIR-V backends compile through, so every #if block is resolved and the stored text
/// carries only live code), then rewrites the preprocessed 450 text to 330 (lower the version
/// header, drop the set/binding layout members, drop varying locations, fold location ids to
/// literals). Returns an empty string when preprocessing fails or the source does not carry a
/// #version 450 header, so the caller fails the variant closed.
std::string EmitDirectGLSL330(const std::string& source, ShaderStageType stage,
                              std::string* error);

/// Emits the GLES 300 spelling of one stage: the same preprocessing and rewrites as
/// EmitDirectGLSL330, plus the ES-specific ones — the `#version 300 es` header with the two
/// default precision lines, a blanket `highp` decoration on every float-family declaration (ES
/// defaults to mediump, which many devices implement as fp16, so the decoration is a numerical
/// contract), and, for fbf variants, the GL_EXT_shader_framebuffer_fetch dialect in place of the
/// subpassInput/subpassLoad syntax. Returns an empty string on the same failure conditions.
std::string EmitDirectGLSLES300(const std::string& source, ShaderStageType stage, bool fbfVariant,
                                std::string* error);

/// Evaluates an integer constant expression of a layout id after preprocessing: decimal
/// literals, unary +/-, binary + - *, and parentheses. Returns false on any other form (an
/// identifier included, and a literal beyond the int64 range), so the caller keeps the original
/// text instead of storing a wrong literal.
bool FoldIntegerExpression(const std::string& expr, int64_t* value);

}  // namespace tgfx
