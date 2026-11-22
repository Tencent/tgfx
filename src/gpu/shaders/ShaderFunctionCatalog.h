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

#include <set>
#include <string>

// The generated catalog the shader build tool scans from the fn directory and the geometry
// coverage sources. Every build regenerates the header (the --catalog custom command), so the
// binary always carries the current function sources.
#if __has_include("ChainFunctionCatalog.h")
#include "ChainFunctionCatalog.h"
#define TGFX_HAS_FUNCTION_CATALOG 1
#endif

namespace tgfx {

/**
 * Appends the named function's GLSL text and its transitive dependencies to out, in dependency
 * order (every callee is spelled before its caller), deduplicated by name through emitted. The set
 * belongs to one assembling shader: two shaders never share it.
 * @param name The catalog entry name, e.g. "pmApplyLuma".
 * @param emitted The dedup set owned by the assembling shader.
 * @param out The function-section text the sources append to.
 * @return True when the catalog exists and names an entry. On false the caller must not emit the
 *         call site: the shader would reference an undefined function.
 */
bool AppendShaderFunction(const char* name, std::set<std::string>* emitted, std::string* out);

}  // namespace tgfx
