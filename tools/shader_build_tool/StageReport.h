/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "BundleWriter.h"

namespace tgfx {

/// Writes the per-stage audit report as JSON. One entry per logical stage (family, stage,
/// permutation index, profile) with dimension values, the bundle stage key, code bytes and hash,
/// the serialized reflection hash, and cross-family duplicate attribution; plus per-profile
/// summary counts. The three counts answer different questions and must stay separate:
/// logicalStages counts declared stage records, uniqueCode counts distinct code blobs, and
/// uniqueCodeReflection counts distinct code+reflection combinations (reflection can differ
/// while code matches). Returns false when the output file cannot be written.
bool WriteStageReport(const std::string& path, const std::vector<VariantData>& variants);

}  // namespace tgfx
