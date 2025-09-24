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

#include "tgfx/core/Point.h"

namespace tgfx {
enum class SortOrder { Ascending, Descending };

/**
 * Compares two tiles based on their distance to a center point.
 * @param center The center point to measure distance from.
 * @param tileSize The size of each tile.
 * @param a The coordinates of the first tile (tileX, tileY).
 * @param b The coordinates of the second tile (tileX, tileY).
 * @param order The sort order, either ascending or descending.
 * @return True if tile 'a' is closer to the center than tile 'b' (for ascending order),
 *         or farther from the center than tile 'b' (for descending order).
 */
bool TileSortCompareFunc(const Point& center, float tileSize, const std::pair<int, int>& a,
                         const std::pair<int, int>& b, SortOrder order = SortOrder::Ascending);
}  // namespace tgfx
