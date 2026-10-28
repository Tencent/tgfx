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
#include "core/Diagnostics.h"
#include "core/ShaderSources.h"
#include "core/Types.h"

namespace tgfx {

/// Writes the per-variant stage dump that two builds are compared with: one JSON file per
/// (profile, family, variant) under <dir>/<profile>/ holding the code blob hash and the full
/// reflection (uniform names/formats, sampler names, vertex input names/formats/locations), and
/// the expanded include text of every family stage under <dir>/expanded/. A reorganization that
/// moves files without changing the compiled output keeps every hash and every reflection entry
/// identical, so a diff of two dumps attributes each change to the step that caused it.
/// Returns false when a file cannot be written, reporting the failure to diagnostics.
bool WriteStageDump(const std::string& dir, const std::vector<VariantData>& variants,
                    const ShaderSourceSet& sources, Diagnostics* diagnostics);

}  // namespace tgfx
