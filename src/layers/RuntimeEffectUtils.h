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

#include "tgfx/core/Point.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {

/**
 * Fills a 16-float vertex buffer for a full-screen triangle strip quad.
 * Layout: (ndcX, ndcY, texU, texV) × 4 vertices (bottom-left, bottom-right, top-left, top-right).
 */
inline void CollectFullQuadVertices(const Texture* source, const Texture* target,
                                    const Point& offset, float* vertices) {
  auto sourceWidth = static_cast<float>(source->width());
  auto sourceHeight = static_cast<float>(source->height());
  auto targetWidth = static_cast<float>(target->width());
  auto targetHeight = static_cast<float>(target->height());

  float left = offset.x;
  float top = offset.y;
  float right = offset.x + sourceWidth;
  float bottom = offset.y + sourceHeight;

  float ndcLeft = 2.0f * left / targetWidth - 1.0f;
  float ndcRight = 2.0f * right / targetWidth - 1.0f;
  float ndcTop = 2.0f * top / targetHeight - 1.0f;
  float ndcBottom = 2.0f * bottom / targetHeight - 1.0f;

  size_t i = 0;
  // Bottom-left
  vertices[i++] = ndcLeft;
  vertices[i++] = ndcBottom;
  vertices[i++] = 0.0f;
  vertices[i++] = 1.0f;
  // Bottom-right
  vertices[i++] = ndcRight;
  vertices[i++] = ndcBottom;
  vertices[i++] = 1.0f;
  vertices[i++] = 1.0f;
  // Top-left
  vertices[i++] = ndcLeft;
  vertices[i++] = ndcTop;
  vertices[i++] = 0.0f;
  vertices[i++] = 0.0f;
  // Top-right
  vertices[i++] = ndcRight;
  vertices[i++] = ndcTop;
  vertices[i++] = 1.0f;
  vertices[i++] = 0.0f;
}

}  // namespace tgfx
