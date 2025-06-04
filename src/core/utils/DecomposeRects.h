/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
/**
 * Restructure a list of rectangles to remove their intersections while still covering the same or a
 * larger area. The input rectangles are modified in place.
 * @param rects Array of rectangles to decompose.
 * @param count Number of rectangles in the array.
 */
void DecomposeRects(Rect* rects, size_t count);
}  // namespace tgfx
