/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "DrawContext.h"

namespace tgfx {
DrawContext::DrawContext() {
  // Set the clip to wide open.
  state.clip.toggleInverseFillType();
}

void DrawContext::save() {
  stack.push(state);
}

void DrawContext::restore() {
  if (stack.empty()) {
    return;
  }
  state = stack.top();
  stack.pop();
}

void DrawContext::translate(float dx, float dy) {
  state.matrix.preTranslate(dx, dy);
}

void DrawContext::scale(float sx, float sy) {
  state.matrix.preScale(sx, sy);
}

void DrawContext::rotate(float degrees) {
  state.matrix.preRotate(degrees);
}

void DrawContext::rotate(float degress, float px, float py) {
  state.matrix.preRotate(degress, px, py);
}

void DrawContext::skew(float sx, float sy) {
  state.matrix.preSkew(sx, sy);
}

void DrawContext::concat(const Matrix& matrix) {
  state.matrix.preConcat(matrix);
}

void DrawContext::setMatrix(const Matrix& matrix) {
  state.matrix = matrix;
}

void DrawContext::resetMatrix() {
  state.matrix.reset();
}

void DrawContext::clipRect(const tgfx::Rect& rect) {
  Path path = {};
  path.addRect(rect);
  clipPath(path);
}

void DrawContext::clipPath(const Path& path) {
  auto clipPath = path;
  clipPath.transform(state.matrix);
  state.clip.addPath(clipPath, PathOp::Intersect);
}
}  // namespace tgfx