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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/aot/write/ModuleWriter.h"

namespace tgfx {

struct Module;

/**
 * The geometry modules' writers. A geometry module reads only the draw's geometry parameters, so
 * they share one file instead of one file per module: they are one kind of thing, not independent
 * features. The modules whose writer needs the expression's leaves (Mesh, ShapeInstanced,
 * BlurSource) and the ones sharing the clip helper (the hairlines) still live in the shared
 * writer until those helpers move.
 */
ModuleWriter GeometryWriterOf(const Module& module);

}  // namespace tgfx
