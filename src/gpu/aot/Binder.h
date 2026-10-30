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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/UniformData.h"
#include "gpu/aot/Program.h"

namespace tgfx {

class UniformData;

/**
 * Writes one compiled chain program's uniforms: the vertex-side coord-source mask and every
 * fragment field the kernel reads — the slot records, the coverage arrays, the tiled recipes, the
 * gradient and noise parameter blocks. This is the shell-free upload path: it reads the program
 * and its binding table only, never a FragmentProcessor. The per-binding child uniforms (a leaf's
 * Subset/AlphaOnly/RGBAAA, the mask decoys) join on the binder's per-binding walk.
 */
void BindChainProgram(const AOTProgram& program, UniformData* vertexUniformData,
                      UniformData* fragmentUniformData);

}  // namespace tgfx
