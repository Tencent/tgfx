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

#include <cstddef>
#include <vector>
#include "tgfx/core/Rect.h"

namespace tgfx {
/**
 * A set of tile rects that share one backdrop capture: `indices` point into the input list and
 * `bounds` is their union.
 */
struct TileCluster {
  std::vector<size_t> indices = {};
  Rect bounds = Rect::MakeEmpty();
};

/**
 * Groups rects so that each group can be captured with one surface. A rect joins an existing
 * cluster while the union of that cluster stays within `maxGrowth` times the sum of the member
 * areas; otherwise it starts a new cluster. With maxGrowth = 2 a cluster can be a block of adjacent
 * tiles (union equals the sum), but not a sparse set: a diagonal pair already costs twice the sum,
 * and anything further apart is worse.
 *
 * All rects must be expressed in the same space. In the tile pipeline that means the same level and
 * without the atlas slot offset, because a cluster shares one capture transform.
 */
inline std::vector<TileCluster> ClusterTileRects(const std::vector<Rect>& rects,
                                                 double maxGrowth = 2.0) {
  std::vector<TileCluster> clusters = {};
  std::vector<double> sumAreas = {};
  clusters.reserve(rects.size() / 2 + 1);
  sumAreas.reserve(rects.size() / 2 + 1);
  for (size_t index = 0; index < rects.size(); ++index) {
    const auto& rect = rects[index];
    if (rect.isEmpty()) {
      continue;
    }
    auto area = static_cast<double>(rect.width()) * static_cast<double>(rect.height());
    bool merged = false;
    for (size_t i = 0; i < clusters.size(); ++i) {
      auto unionRect = clusters[i].bounds;
      unionRect.join(rect);
      auto unionArea =
          static_cast<double>(unionRect.width()) * static_cast<double>(unionRect.height());
      if (unionArea <= maxGrowth * (sumAreas[i] + area)) {
        clusters[i].indices.push_back(index);
        clusters[i].bounds = unionRect;
        sumAreas[i] += area;
        merged = true;
        break;
      }
    }
    if (!merged) {
      TileCluster cluster = {};
      cluster.indices.push_back(index);
      cluster.bounds = rect;
      clusters.push_back(std::move(cluster));
      sumAreas.push_back(area);
    }
  }
  return clusters;
}
}  // namespace tgfx
