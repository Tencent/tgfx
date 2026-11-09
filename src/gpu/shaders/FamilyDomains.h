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

#include "gpu/shaders/Family.h"
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/**
 * The permutation-domain view of a declaration-driven family: the free dims in flattened module
 * order (geometry first, body modules, Xfer last), which is exactly the radix order the variant
 * ordinal encodes. Both the build tool (bundle emission) and the runtime registry (the
 * post-match buildable check and the FBF profile filter) read this one conversion, so the
 * ordinal, the bundle keys and the domain can never disagree.
 */
PrecompiledShaderInfo AssembledFamilyInfo(const Family& family);

}  // namespace tgfx
