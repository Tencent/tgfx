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

#include "DecomposeRects.h"
#include "core/utils/Log.h"

namespace tgfx {
static void DecomposeRect(Rect* rectA, Rect* rectB) {
  // Ensure rectA and rectB overlap
  DEBUG_ASSERT(Rect::Intersects(*rectA, *rectB));

  // Step 1: Build the 3 rect slabs on the y-axis
  Rect rects[3];
  if (rectA->top < rectB->top) {
    rects[0].top = rectA->top;
    rects[0].bottom = rectB->top;
    rects[0].left = rectA->left;
    rects[0].right = rectA->right;
  } else {
    rects[0].top = rectB->top;
    rects[0].bottom = rectA->top;
    rects[0].left = rectB->left;

    rects[0].right = rectB->right;
  }
  if (rectA->bottom < rectB->bottom) {
    rects[2].top = rectA->bottom;
    rects[2].bottom = rectB->bottom;
    rects[2].left = rectB->left;
    rects[2].right = rectB->right;
  } else {
    rects[2].top = rectB->bottom;
    rects[2].bottom = rectA->bottom;
    rects[2].left = rectA->left;
    rects[2].right = rectA->right;
  }
  rects[1].top = rects[0].bottom;
  rects[1].bottom = rects[2].top;
  rects[1].left = std::min(rectA->left, rectB->left);
  rects[1].right = std::max(rectA->right, rectB->right);

  // Step 2: Compute areas
  float areas[3];
  for (int i = 0; i < 3; ++i) {
    areas[i] = rects[i].area();
  }

  // Step 3: Union slabs and compute area deltas
  Rect u1 = rects[0];
  u1.join(rects[1]);
  Rect u2 = rects[1];
  u2.join(rects[2]);
  float delta0 = areas[0] + areas[1] - u1.area();
  float delta1 = areas[1] + areas[2] - u2.area();

  // Step 4: Choose the best decomposition
  if (delta0 > delta1) {
    *rectA = u1;
    *rectB = rects[2];
  } else {
    *rectA = rects[0];
    *rectB = u2;
  }
}

void DecomposeRects(Rect* rects, size_t count) {
  for (size_t i = 0; i < count; i++) {
    for (size_t j = i + 1; j < count; j++) {
      if (Rect::Intersects(rects[i], rects[j])) {
        DecomposeRect(&rects[i], &rects[j]);
      }
    }
  }
}
}  // namespace tgfx
