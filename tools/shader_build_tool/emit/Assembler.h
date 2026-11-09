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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "core/Types.h"
#include "gpu/shaders/Family.h"

namespace tgfx {

/// One assembled variant: the two stage texts (all constants — locations, bindings and block
/// members are already resolved, no #if over the interface remains) and the dim defines the
/// module-internal code still reads.
struct AssembledVariant {
  std::string vertText;
  std::string fragText;
  std::vector<std::string> vertDefines;
  std::vector<std::string> fragDefines;
};

/// Assembles one variant of the family from the module declarations. The layout derives from the
/// same declarations (DeriveLayout), so the assembled interface and the derived numbers cannot
/// drift; the build verifies this against the SPIR-V reflection before emitting.
AssembledVariant AssembleVariant(const Family& family, const Variant& variant);

}  // namespace tgfx
