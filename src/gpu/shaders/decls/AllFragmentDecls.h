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

namespace tgfx {

/// The hand-written fragment declarations the emitter still walks. The chain interpreter's was
/// the last one (the declaration layer supersedes them); the list stays as the emitter's seam, and
/// a family that still needs a hand-written entry joins here.
inline const std::vector<const FragmentFamilyDecl*>& AllFragmentDecls() {
  static const std::vector<const FragmentFamilyDecl*> decls = {};
  return decls;
}

}  // namespace tgfx
