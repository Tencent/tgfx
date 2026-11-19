/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <algorithm>

namespace tgfx {

// Layer effects can magnify their inputs well beyond the render target. Keep their intermediate
// textures within explicit effect budgets, while still respecting the active GPU.
constexpr int DefaultEffectTextureDimension = 2048;
constexpr int TextureFilterTextureDimension = DefaultEffectTextureDimension;

constexpr int ResolveEffectTextureDimension(int gpuLimit, int effectLimit) {
  return std::min(gpuLimit, effectLimit);
}

}  // namespace tgfx
