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

/// A device-space alpha mask folded into the coverage: the shader samples the mask at
/// gl_FragCoord through the module's transform. The former fill pipeline's
/// FILL_DEVICE_MASK_ALWAYS.
inline constexpr ModuleDim DeviceMaskDims[] = {{"PRESENT", 2}};

inline constexpr Element DeviceMaskElements[] = {
    {Element::Kind::Param, ShaderType::Mat3, "CoordMatrix", 1, 0},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler", 1, 0},
};

inline constexpr Module DeviceMask = {"DeviceMask", DeviceMaskDims, 1, DeviceMaskElements, 2, false,
                                      true};

}  // namespace tgfx
