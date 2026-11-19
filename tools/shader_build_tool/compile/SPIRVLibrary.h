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
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgfx {

/**
 * Turns one compiled wrapper module into a linkable SPIR-V library: the named function gains an
 * Export linkage decoration, the dummy entry point and its function body disappear, and the
 * Linkage capability rides along. The patch is instruction-level word surgery — whole
 * instructions inserted or removed at their section boundaries — so the module never needs a
 * disassembly round trip and introduces no dependency on SPIRV-Tools.
 * @param words         The compiled module (the wrapper's entry calls the exported function).
 * @param functionName  The GLSL function to export; the export is matched to the one function the
 *                      wrapper's entry calls.
 * @param error         Receives the failure reason.
 * @return              The patched module, or empty on failure.
 */
std::vector<uint32_t> ExportSPIRVFunctionForLinking(const std::vector<uint32_t>& words,
                                                    const char* functionName, std::string* error);

/**
 * Builds one operator's linkable SPIR-V library module: compiles the wrapper (a fragment whose
 * dummy entry calls the operator, forcing its emission) and patches the result for linking.
 * @param wrapperSource  The wrapper's GLSL text, includes already expanded.
 * @param functionName   The operator's GLSL function name; the export carries this string.
 * @param shaderName     The shader name the compile diagnostics report.
 * @param error          Receives the compile or patch failure.
 * @return               The linkable module, or empty on failure.
 */
std::vector<uint32_t> CompileSPIRVLibraryModule(const std::string& wrapperSource,
                                                const char* functionName, const char* shaderName,
                                                std::string* error);

/**
 * Turns one compiled frame module into one that imports the named function: the stub's body
 * disappears (its parameters stay), an Import linkage decoration goes on, and the Linkage
 * capability rides along. The entry point and the remaining functions stay untouched — this is
 * the frame's shape, an entry point that calls a body the runtime supplies.
 * @param words         The compiled frame (its dummy entry calls the stub by name).
 * @param functionName  The GLSL function to import; the body module exports this same string.
 * @param error         Receives the failure reason.
 * @return              The patched module, or empty on failure.
 */
std::vector<uint32_t> ImportSPIRVFunction(const std::vector<uint32_t>& words,
                                          const char* functionName, std::string* error);

}  // namespace tgfx
