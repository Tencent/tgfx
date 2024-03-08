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

#include "MCStack.h"

namespace tgfx {
MCStack::MCStack(const Path& initClip) : state({Matrix::I(), initClip}) {
}

MCStack::MCStack(const Matrix& initMatrix, const Path& initClip)
    : initMatrix(new Matrix(initMatrix)), state({initMatrix, initClip}) {
}

MCStack::~MCStack() {
  delete initMatrix;
}

void MCStack::save() {
  stack.push(state);
}

void MCStack::restore() {
  if (stack.empty()) {
    return;
  }
  state = stack.top();
  stack.pop();
}

void MCStack::translate(float dx, float dy) {
  state.matrix.preTranslate(dx, dy);
}

void MCStack::scale(float sx, float sy) {
  state.matrix.preScale(sx, sy);
}

void MCStack::rotate(float degrees) {
  state.matrix.preRotate(degrees);
}

void MCStack::rotate(float degress, float px, float py) {
  state.matrix.preRotate(degress, px, py);
}

void MCStack::skew(float sx, float sy) {
  state.matrix.preSkew(sx, sy);
}

void MCStack::concat(const Matrix& matrix) {
  state.matrix.preConcat(matrix);
}

void MCStack::setMatrix(const Matrix& matrix) {
  state.matrix = matrix;
  if (initMatrix != nullptr) {
    state.matrix.postConcat(*initMatrix);
  }
}

void MCStack::resetMatrix() {
  state.matrix = initMatrix ? *initMatrix : Matrix::I();
}

void MCStack::clipRect(const tgfx::Rect& rect) {
  Path path = {};
  path.addRect(rect);
  clipPath(path);
}

void MCStack::clipPath(const Path& path) {
  auto clipPath = path;
  clipPath.transform(state.matrix);
  state.clip.addPath(clipPath, PathOp::Intersect);
}
}  // namespace tgfx
