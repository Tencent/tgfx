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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/spec/SpecValues.h"

namespace tgfx {

/**
 * The text backends' injection: rewrites the generated `TGFX_SPEC_<id> = 0` declarations of a
 * stored GLSL artifact to this draw's values, so the driver's compiler folds them (the master
 * switch turns the accessors' constant branch on). Values equal to the default are left alone,
 * and every non-default id must find exactly one declaration — an artifact that lost one fails
 * the injection, which the caller answers with the generic form.
 * @param code    The stored fragment-stage GLSL text.
 * @param fields  The variant's spec table (the id assignment order).
 * @param values  The captured values (the master switch first).
 * @return        The specialized text, or an empty string when the artifact does not carry the
 *                expected declarations.
 */
std::string InjectSpecGLSL(const std::string& code, const std::vector<SpecFieldInfo>& fields,
                           const SpecValues& values);

}  // namespace tgfx
