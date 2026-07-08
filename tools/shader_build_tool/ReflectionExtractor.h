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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "BundleWriter.h"

namespace tgfx {

/// Extracts reflection data (uniform block members and samplers) from compiled SPIR-V binaries
/// using spirv-cross. The extracted information is used to populate the ReflectionData in the
/// shader bundle, enabling the runtime to reconstruct UniformData without recompilation.
ReflectionData ExtractReflection(const std::vector<uint32_t>& vertexSPIRV,
                                 const std::vector<uint32_t>& fragmentSPIRV);

}  // namespace tgfx
