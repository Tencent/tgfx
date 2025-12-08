/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "TextShape.h"
#include "core/GlyphRunList.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::MakeFrom(std::shared_ptr<TextBlob> textBlob) {
  auto glyphRunLists = GlyphRunList::Unwrap(textBlob.get());
  if (glyphRunLists == nullptr) {
    return nullptr;
  }
  std::vector<std::shared_ptr<Shape>> shapes;
  for (auto& list : *glyphRunLists) {
    if (list->hasOutlines()) {
      shapes.push_back(std::make_shared<TextShape>(list));
    }
  }
  if (shapes.empty()) {
    return nullptr;
  }
  if (shapes.size() == 1) {
    return shapes[0];
  }
  return Shape::Merge(shapes);
}

Rect TextShape::onGetBounds() const {
  auto bounds = glyphRunList->getBounds();
  return bounds;
}

Path TextShape::onGetPath(float resolutionScale) const {
  if (FloatNearlyZero(resolutionScale)) {
    return {};
  }
  Path path = {};
  auto matrix = Matrix::MakeScale(resolutionScale, resolutionScale);
  if (!glyphRunList->getPath(&path, &matrix)) {
    LOGE("TextShape::getPath() Failed to get path from GlyphRunList!");
    return {};
  }
  auto inverseMatrix = Matrix::MakeScale(1.f / resolutionScale, 1.f / resolutionScale);
  path.transform(inverseMatrix);
  return path;
}
}  // namespace tgfx
