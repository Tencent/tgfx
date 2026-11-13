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

#include "gpu/aot/ir/GeometryParameters.h"
#include "gpu/aot/select/FamilyMatchResult.h"

namespace tgfx {

class ProgramInfo;
struct Module;
class SlotWriter;

/**
 * One module's whole contribution to a draw's upload. The walk visits the variant's modules in
 * declaration order and addresses each field through the derived layout, so a module never reasons
 * about the modules around it. A module writer lives in its own file under write/modules/, next to
 * the declaration it serves.
 */
struct WriteContext {
  const ProgramInfo& info;
  const FamilyMatchResult& sources;
  const AOTGeometryParameters& geometry;
  SlotWriter* slot = nullptr;
};

/// One module's write. Batch by batch, the modules move out of DeclaredFamilyWriter's shared
/// switch into these per-module functions.
using ModuleWriter = void (*)(WriteContext& context);

/**
 * The migrated writer for a module, or null while the module still lives in the shared switch.
 * The table is the migration's frontier: once every module returns a writer here, the switch and
 * the shared file's module includes go away.
 */
ModuleWriter WriterOf(const Module& module);

}  // namespace tgfx
