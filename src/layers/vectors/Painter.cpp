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

#include "Painter.h"
#include "tgfx/layers/LayerRecorder.h"

namespace tgfx {

void Painter::applyTransform(const Matrix& groupMatrix, float groupAlpha) {
  matrix.postConcat(groupMatrix);
  alpha *= groupAlpha;
}

void Painter::offsetShapeIndex(size_t offset) {
  startIndex += offset;
}

void Painter::draw(LayerRecorder* recorder, const std::vector<std::shared_ptr<Shape>>& shapes) {
  for (size_t i = 0; i < matrices.size(); i++) {
    auto& shape = shapes[startIndex + i];
    if (shape == nullptr) {
      continue;
    }
    auto finalShape = Shape::ApplyMatrix(shape, matrices[i]);
    onDraw(recorder, std::move(finalShape));
  }
}

}  // namespace tgfx
