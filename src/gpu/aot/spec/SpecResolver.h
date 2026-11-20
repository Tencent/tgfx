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

#include <cstdint>
#include "gpu/aot/spec/SpecValues.h"
#include "gpu/shaders/Family.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/ShaderModule.h"

namespace tgfx {

/// The variant's specialization table, derived from its declaration: the same order, names and id
/// assignment the assembler declares them with.
std::vector<SpecFieldInfo> DeriveSpecFieldInfos(const Variant& variant);

/// The code format a backend's payload arrives in. The backend picks the dialect and the bundle
/// header says whether that dialect arrives as driver source text or as a precompiled container;
/// Metal and D3D12 support both forms, so their choice is read from the bundle rather than
/// assumed. Shared with the precompiled creator, whose module descriptors name the same format.
ShaderCodeFormat SpecFormatForBackend(Backend backend, uint16_t payloadFormat);

}  // namespace tgfx
