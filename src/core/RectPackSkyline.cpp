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

#include "RectPackSkyline.h"
#include "../../include/tgfx/core/Log.h"

namespace tgfx {
bool RectPackSkyline::addRect(int width, int height, Point& location) {
  if (width > _width || height > _height) {
    return false;
  }
  auto bestWidth = _width + 1;
  auto bestIndex = -1;
  auto bestX = 0;
  auto bestY = _height + 1;
  for (size_t i = 0; i < skyline.size(); ++i) {
    int y;
    if (rectangleFits(static_cast<int>(i), width, height, y)) {
      if (y < bestY || (y == bestY && skyline[i].width < bestWidth)) {
        bestIndex = static_cast<int>(i);
        bestWidth = skyline[i].width;
        bestX = skyline[i].x;
        bestY = y;
      }
    }
  }
  if (bestIndex != -1) {
    addSkylineLevel(bestIndex, bestX, bestY, width, height);
    location = Point::Make(bestX, bestY);
    areaSoFar += width * height;
    return true;
  }
  location = Point::Zero();
  return false;
}

bool RectPackSkyline::rectangleFits(int skylineIndex, int width, int height, int& yPosition) const {
  auto x = skyline[static_cast<size_t>(skylineIndex)].x;
  if (x + width > _width) {
    return false;
  }
  auto widthLeft = width;
  auto i = static_cast<size_t>(skylineIndex);
  auto y = skyline[i].y;
  while (widthLeft > 0) {
    y = std::max(y, skyline[i].y);
    if (y + height > _height) {
      return false;
    }
    widthLeft -= skyline[i].width;
    ++i;
    DEBUG_ASSERT(i < skyline.size() || widthLeft <= 0);
  }
  yPosition = y;
  return true;
}

void RectPackSkyline::addSkylineLevel(int skylineIndex, int x, int y, int width, int height) {
  Node newNode = {x, y + height, width};
  skyline.insert(skyline.begin() + skylineIndex, newNode);

  // delete width of the new skyline segment from following ones
  for (auto i = static_cast<size_t>(skylineIndex + 1); i < skyline.size(); ++i) {
    // Skip if no overlap
    if (skyline[i].x >= skyline[i - 1].x + skyline[i - 1].width) {
      break;
    }
    int shrink = skyline[i - 1].x + skyline[i - 1].width - skyline[i].x;
    skyline[i].x += shrink;
    skyline[i].width -= shrink;
    if (skyline[i].width > 0) {
      // only partially consumed
      break;
    }
    // fully consumed
    skyline.erase(skyline.begin() + static_cast<int>(i));
    --i;
  }
  // merge skyline
  for (size_t i = 0; i < skyline.size() - 1; ++i) {
    if (skyline[i].y != skyline[i + 1].y) {
      continue;
    }
    skyline[i].width += skyline[i + 1].width;
    skyline.erase(skyline.begin() + static_cast<int>(i) + 1);
    --i;
  }
}
}  // namespace tgfx
