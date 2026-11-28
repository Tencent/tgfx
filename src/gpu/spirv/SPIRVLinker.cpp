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

#include "gpu/spirv/SPIRVLinker.h"
// The linking route exists only where Vulkan does: the SPIRV-Tools sources the runtime links
// against are built for the Vulkan configuration alone. Other backends get the declining stubs
// below, so the creator's Vulkan branch collapses to its fallback.
// The runtime link route serves both link-less backends: Vulkan links the module pairs into a
// binary module, Metal links the same modules and then translates the result to MSL.
#if defined(TGFX_USE_VULKAN) || defined(TGFX_USE_METAL)

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include "gpu/spirv/SPIRVWriter.h"
#include "spirv-tools/libspirv.hpp"
#include "spirv-tools/linker.hpp"

namespace tgfx {

// The runtime's own gate: every module and the linked result are validated before a pipeline is
// built from them, so a malformed emission falls back to the interpreter instead of reaching the
// driver.
bool ValidateSPIRVModule(const std::vector<uint32_t>& words, std::string* error) {
  spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_0);
  std::string diagnostic = {};
  tools.SetMessageConsumer(
      [&diagnostic](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        if (message != nullptr) {
          diagnostic = message;
        }
      });
  if (!tools.Validate(words)) {
    *error = diagnostic.empty() ? "the module failed validation" : diagnostic;
    return false;
  }
  return true;
}

bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>& modules, std::vector<uint32_t>* out,
                    std::string* error) {
  if (out == nullptr || error == nullptr) {
    return false;
  }
  if (modules.size() < 2) {
    *error = "the link needs a frame, a body and at least one op";
    return false;
  }
  spvtools::Context context(SPV_ENV_VULKAN_1_0);
  std::string diagnostic = {};
  context.SetMessageConsumer(
      [&diagnostic](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        if (message != nullptr) {
          diagnostic = message;
        }
      });
  spvtools::LinkerOptions options = {};
  options.SetVerifyIds(true);
  out->clear();
  if (spvtools::Link(context, modules, out, options) != SPV_SUCCESS) {
    *error = diagnostic.empty() ? "the modules did not link" : diagnostic;
    return false;
  }
  return ValidateSPIRVModule(*out, error);
}

}  // namespace tgfx

#else  // TGFX_USE_VULKAN || TGFX_USE_METAL

namespace tgfx {

bool LinkChainSPIRV(const std::vector<std::vector<uint32_t>>&, std::vector<uint32_t>*,
                    std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan linking route is not part of this build";
  }
  return false;
}

bool ValidateSPIRVModule(const std::vector<uint32_t>&, std::string* error) {
  if (error != nullptr) {
    *error = "the Vulkan linking route is not part of this build";
  }
  return false;
}

}  // namespace tgfx

#endif  // TGFX_USE_VULKAN || TGFX_USE_METAL
