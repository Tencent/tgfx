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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/aot/ir/Program.h"
#include "gpu/aot/spec/SpecValues.h"

namespace tgfx {

/// The chain family's specialization values, read straight off the compiled program: the master
/// switch, the live slot count, the packed slot records (the inactive tail carries the zeroed
/// record, exactly what the runtime binder uploads) and the three register indices. The order is
/// the bundle's spec table, so the values a specialized shader folds are the values the runtime
/// would have uploaded. Returns an empty set when the table names a field this family does not
/// have, so a drifted declaration fails closed instead of specifying nothing.
SpecValues ResolveChainSpec(const AOTProgram& program, const std::vector<SpecFieldInfo>& fields);

}  // namespace tgfx
