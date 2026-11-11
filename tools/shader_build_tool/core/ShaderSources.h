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

#include <cstddef>
#include <string>

namespace tgfx {

/// The deepest include nesting the expander below accepts.
inline constexpr int MaxIncludeDepth = 32;

/// Expands every #include of an assembled stage text against shaderDir — the same expansion the
/// bundle build applies to the assembled variants, so the text the compiler receives is the text
/// the assembler emitted. Returns an empty string on failure.
std::string ExpandStageIncludes(const std::string& source, const std::string& shaderDir,
                                std::string* error);

}  // namespace tgfx
