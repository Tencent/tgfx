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

#include <map>
#include <string>

namespace tgfx {

struct FragmentFamilyDecl;

/// Emits the fragment entry text of every declared family, keyed by the family's file path — the
/// same path LoadShaderSources matches a family's fragment file against, so the caller can hand
/// the map back as the stage override. Returns an empty map and sets error when a declaration
/// cannot be expressed.
std::map<std::string, std::string> EmitDeclaredFragmentEntries(std::string* error);

/// Emits one family's fragment entry text (see above). Returns an empty string and sets error on
/// a declaration the emitter cannot fill in (an array field, a malformed define).
std::string EmitFragmentEntry(const FragmentFamilyDecl& decl, std::string* error);

}  // namespace tgfx
