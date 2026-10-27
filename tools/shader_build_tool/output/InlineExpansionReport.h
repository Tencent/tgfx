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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "core/Diagnostics.h"
#include "core/Types.h"

namespace tgfx {

/// Writes the per-variant expansion and every duplicated function to a JSON file, stating the
/// same policy limit the verify rule enforces (MaxDuplicatedInstructions, core/Types.h). Returns
/// false when the file cannot be written, reporting the failure to diagnostics.
bool WriteInlineExpansionReport(const std::string& path,
                                const std::vector<InlineExpansionRecord>& records,
                                Diagnostics* diagnostics);

}  // namespace tgfx
