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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "gpu/shaders/decls/PointwiseChainDecl.h"
#include "gpu/shaders/decls/QuadTextureFillDecl.h"

namespace tgfx {

/// The declared fragment families, in the fixed order the emitter walks them. The list is short
/// on purpose: only the pilot families are migrated, and it grows as the migration proceeds. A
/// declaration joins by being added here; the family's shell (kernels/*.h) stays the registry
/// entry, and the two agree on the family name (pinned by a static_assert in every shell).
inline const std::vector<const FragmentFamilyDecl*>& AllFragmentDecls() {
  static const std::vector<const FragmentFamilyDecl*> decls = {&QuadTextureFillDecl::Decl,
                                                               &PointwiseChainDecl::Decl};
  return decls;
}

}  // namespace tgfx
