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

namespace tgfx {

/**
 * Appends the named GLSL function and its transitive dependencies to out, in dependency order
 * (every callee is spelled before its caller), deduplicated by name through emitted. The set
 * belongs to one assembling shader: two shaders never share it. The sources come from the
 * shader library bundle embedded into this build.
 * @param name The bundle entry name, e.g. "pmApplyLuma".
 * @param emitted The dedup set owned by the assembling shader.
 * @param out The function-section text the sources append to.
 * @return True when the bundle exists and names an entry. On false the caller must not emit the
 *         call site: the shader would reference an undefined function.
 */
bool AppendShaderFunction(const char* name, std::set<std::string>* emitted, std::string* out);

}  // namespace tgfx
