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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/**
 * Translates a linked chain module (the frame's metal entry plus the runtime body and the op
 * functions) into MSL source the Metal backend compiles at pipeline creation. The translation
 * mirrors the build tool's: binding numbers map straight onto the MSL buffer/texture/sampler
 * index spaces, which is the convention the runtime's metal descriptors already carry.
 */
bool TranslateChainSPIRVToMSL(const std::vector<uint32_t>& words, std::string* msl,
                              std::string* error);

}  // namespace tgfx
