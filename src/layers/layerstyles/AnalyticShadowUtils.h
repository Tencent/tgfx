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
#include <utility>
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

/**
 * Returns the geometry to evaluate a closed-form shadow from, or nullopt when the layer cannot use
 * that path and the caller must fall back to the filter path. Requires a fill-only shape whose
 * outline is exact and reduces to a Rect, a uniformly rounded RRect or an Oval, optionally placed
 * by a translation and axis-aligned scale.
 *
 * The first element is the spread-adjusted bounds; the second is the corner radius shared by all
 * four corners, per axis. The radius components may differ, describing an elliptical corner, and are
 * both zero for a sharp rectangle.
 *
 * The returned geometry is ready to draw with in a LayerStyle's onDraw: it is measured in content
 * pixels, the same space the shadow's sigma uses, with the origin the canvas already carries.
 * @param input the layer style input to read the contour shape from.
 * @param spread the spread to fold into the geometry. Positive outsets, negative insets.
 */
std::optional<std::pair<Rect, Point>> MakeAnalyticShadowShape(const LayerStyleInput& input,
                                                              float spread);

}  // namespace tgfx
