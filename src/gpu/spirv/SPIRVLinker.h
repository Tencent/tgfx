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
//  license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/**
 * Links the frame, the body and the op modules into one module in the Vulkan 1.0 environment
 * with id verification on. Returns false when an import is unresolved or the ids do not verify —
 * the caller treats that as "the assembled path cannot serve this draw" and keeps its fallback.
 * @param modules  The input modules; the frame (with the entry point) must be among them.
 * @param out      Receives the linked module.
 * @param error    Receives the linker's reason.
 */
bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>& modules, std::vector<uint32_t>* out,
                    std::string* error);

/**
 * Validates one SPIR-V module against Vulkan 1.0. The runtime uses it on the linked result before
 * handing it to the pipeline; the diagnostic path uses it per module.
 */
bool ValidateSPIRVModule(const std::vector<uint32_t>& words, std::string* error);

}  // namespace tgfx
