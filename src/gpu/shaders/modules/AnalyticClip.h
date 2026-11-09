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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

/// The analytic draw clip (an AA rect or rounded rect) through the runtime clip contract: the
/// HasClip uniform selects the branch, so the clip costs no variant.
inline constexpr Element AnalyticClipElements[] = {
    {Element::Kind::Param, ShaderType::Float4, "Rect"},
    {Element::Kind::Param, ShaderType::Int, "HasClip"},
    {Element::Kind::Param, ShaderType::Float4, "LocalRect"},
    {Element::Kind::Param, ShaderType::Float4, "RadiiX"},
    {Element::Kind::Param, ShaderType::Float4, "RadiiY"},
    {Element::Kind::Param, ShaderType::Float, "AntiAlias"},
    {Element::Kind::Param, ShaderType::Mat3, "DeviceToLocal"},
};

inline constexpr Module AnalyticClip = {"AnalyticClip", nullptr, 0, AnalyticClipElements, 7,
                                        false,          true};

}  // namespace tgfx
