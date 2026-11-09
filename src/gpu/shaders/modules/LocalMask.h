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

/// A local-space texture-alpha coverage mask (the former FILL_LOCAL_MASK): the mask samples at its
/// own coordinate transform, its alpha multiplying the running coverage.
inline constexpr ModuleDim LocalMaskDims[] = {{"PRESENT", 2}};

inline constexpr Element LocalMaskElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::Varying, ShaderType::Float3, "Coord"},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
};

inline constexpr Module LocalMask = {
    "LocalMask", LocalMaskDims, 1, LocalMaskElements, std::size(LocalMaskElements), true, true};

}  // namespace tgfx
