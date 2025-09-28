/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/Rect.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
/**
 * Applies the stroke options to the given bounds.
 */
void ApplyStrokeToBounds(const Stroke& stroke, Rect* bounds, bool applyMiterLimit = false);

/**
 * Returns true if the stroke is a hairline (width <= 0).
 */
bool IsHairlineStroke(const Stroke& stroke);
/**
 * If the stroke width is zero or becomes extremely thin after transformation,
 * it can be treated as a hairline to prevent precision issues.
 */
bool TreatStrokeAsHairline(const Stroke& stroke, const Matrix& matrix);

}  // namespace tgfx
