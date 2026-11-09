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

/// The implicit engine module every family carries first: the render-target adjust vector the
/// assembled vertex main multiplies the position with. No dims, no hooks.
inline constexpr Element FrameElements[] = {
    {Element::Kind::VertexParam, ShaderType::Float4, "RTAdjust"},
};

inline constexpr Module Frame = {"Frame", nullptr, 0, FrameElements, 1, false, false};

inline const Module& FrameModule() {
  return Frame;
}

}  // namespace tgfx
