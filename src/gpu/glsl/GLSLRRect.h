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

#include "gpu/FragmentShaderBuilder.h"

namespace tgfx {
/**
 * Emits the shared non-AA round rect SDF coverage block into the fragment shader. Produces the
 * outer coverage and, when stroke is true, the inner coverage, leaving a local `float coverage`
 * declared for the caller to consume. The caller must declare `vec2 halfSize` and `vec2 radii`
 * before invoking this helper. When stroke is true, strokeWidthFsIn must point to the fragment
 * stage input name of the stroke width varying; the helper emits the `vec2 sw = <strokeWidthFsIn>;`
 * declaration itself, so the caller must not pre-declare `sw` in the emitted GLSL.
 *
 * @param fragBuilder the fragment shader builder to emit into.
 * @param centeredPos a GLSL expression evaluating to (localCoord - center).
 * @param stroke whether to emit the inner SDF block for stroke mode.
 * @param strokeWidthFsIn fragment-stage name of the stroke width varying; unused when stroke is
 *        false.
 */
void EmitRRectSDFCoverage(FragmentShaderBuilder* fragBuilder, const char* centeredPos, bool stroke,
                          const char* strokeWidthFsIn);
}  // namespace tgfx
