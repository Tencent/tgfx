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
 * Appends the captured values to a pipeline's fragment-constant table, keyed the way the
 * pipeline-constant backends resolve them: the SpecId in decimal (Metal's MSL function
 * constants carry the SpecId as their index; the Vulkan specialization map keys by SpecId).
 * The master switch is id 0, then every field's words in spec-table order — the same order the
 * values hold.
 * @param fields    The variant's spec table (the id assignment order).
 * @param values    The captured values (the master switch first).
 * @param constants The table to append to.
 */
void AppendSpecConstants(const std::vector<SpecFieldInfo>& fields, const SpecValues& values,
                         std::vector<std::pair<std::string, double>>* constants);

}  // namespace tgfx
