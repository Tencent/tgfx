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

#include <optional>
#include "tgfx/core/RRect.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

/**
 * Returns the geometry used to evaluate a closed-form shadow, or nullopt when a closed-form shadow
 * is not possible for the given input. The spread is folded into the result, outsetting when
 * positive and insetting when negative.
 *
 * The returned RRect is never complex, so all four corners share one radius and either entry of
 * radii() describes the shape. It is measured in content pixels, the same space the shadow's sigma
 * uses, with the origin the canvas already carries.
 */
std::optional<RRect> MakeAnalyticShadowShape(const LayerStyleInput& input, float spread);

}  // namespace tgfx
