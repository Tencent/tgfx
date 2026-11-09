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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

/// A device-space mask folded into the coverage through a runtime branch (the former
/// FILL_DEVICE_MASK_RUNTIME): the fields are declared unconditionally and HasDeviceMask selects
/// the sampling, so one variant serves both masked and unmasked draws — the contract the old
/// QuadTexture and AtlasText kernels encoded.
inline constexpr Element DeviceMaskRuntimeElements[] = {
    {Element::Kind::Param, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::Param, ShaderType::Int, "HasDeviceMask"},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
};

inline constexpr Module DeviceMaskRuntime = {"DeviceMaskRuntime",
                                             nullptr,
                                             0,
                                             DeviceMaskRuntimeElements,
                                             std::size(DeviceMaskRuntimeElements),
                                             false,
                                             true};

}  // namespace tgfx
