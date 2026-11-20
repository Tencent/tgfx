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

#include <optional>
#include "gpu/CoordTransform.h"
#include "gpu/SamplerState.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/SamplingOptions.h"

namespace tgfx {

/// The declared-family writer's leaf-subset facts: the rect and the clamp gate, the same values
/// the module binds compute.
bool LeafSubsetClampOn(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                       const std::optional<Rect>& subset);
void LeafSubsetRectForSampler(const std::shared_ptr<TextureProxy>& proxy, const TextureView* view,
                              const std::optional<Rect>& subset, const SamplerState& sampling,
                              float rect[4]);

/// The device-space mask transform: the leaf's uv matrix scaled into the view's normalized
/// coordinate space. The caller owns the missing-view policy — a chain bind skips the write, a
/// declared writer keeps its own fallback.
Matrix DeviceMaskCoordMatrix(const Matrix& uvMatrix, const TextureView& view);

}  // namespace tgfx
