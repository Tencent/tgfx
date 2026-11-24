/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License") you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "AS IS" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "gpu/spirv/SPIRVInterfaceLayout.h"

namespace tgfx {

struct SPIRVProgramModules {
  std::vector<uint32_t> vertex = {};
  std::vector<uint32_t> fragment = {};
  SPIRVInterfaceLayout layout = {};
};

/// The SPIR-V counterpart of GLSLProgramBuilder. It owns the two stage modules and the
/// descriptor binding contract shared by them.
class SPIRVProgramBuilder {
 public:
  static bool Build(const ProgramIR& vertexProgram, const ProgramIR& fragmentProgram,
                    SPIRVProgramModules* modules, std::string* error = nullptr);
};

}  // namespace tgfx
