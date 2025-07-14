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

#include "core/utils/RectToRectMatrix.h"
namespace tgfx {
Matrix MakeRectToRectMatrix(const Rect& srcRect, const Rect& dstRect) {
  if (srcRect.isEmpty()) {
    return Matrix::I();
  }
  if (dstRect.isEmpty()) {
    return Matrix::MakeAll(0, 0, 0, 0, 0, 0);
  }
  float sx = dstRect.width() / srcRect.width();
  float sy = dstRect.height() / srcRect.height();
  float tx = dstRect.left - srcRect.left * sx;
  float ty = dstRect.top - srcRect.top * sy;
  return Matrix::MakeAll(sx, 0, tx, 0, sy, ty);
}
}  // namespace tgfx
