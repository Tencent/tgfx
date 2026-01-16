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

#include "AppendShape.h"
#include "MergeShape.h"
#include "PathShape.h"

namespace tgfx {
void AppendShape::Append(std::vector<std::shared_ptr<Shape>>* shapes,
                         std::shared_ptr<Shape> shape) {
  if (shape->type() == Type::Append) {
    auto appendShape = std::static_pointer_cast<AppendShape>(shape);
    shapes->insert(shapes->end(), appendShape->shapes.begin(), appendShape->shapes.end());
    return;
  }
  shapes->push_back(std::move(shape));
}

std::shared_ptr<Shape> Shape::Merge(const std::vector<std::shared_ptr<Shape>>& shapes) {
  if (shapes.empty()) {
    return nullptr;
  }
  std::vector<std::shared_ptr<Shape>> list = {};
  for (auto& shape : shapes) {
    AppendShape::Append(&list, shape);
  }
  if (list.size() == 1) {
    return list.front();
  }
  return std::shared_ptr<AppendShape>(new AppendShape(std::move(list)));
}

std::shared_ptr<Shape> AppendShape::MakeFrom(std::shared_ptr<Shape> first,
                                             std::shared_ptr<Shape> second) {
  std::vector<std::shared_ptr<Shape>> shapes = {};
  Append(&shapes, std::move(first));
  Append(&shapes, std::move(second));
  if (shapes.size() == 1) {
    return shapes.front();
  }
  return std::shared_ptr<AppendShape>(new AppendShape(std::move(shapes)));
}

PathFillType AppendShape::fillType() const {
  return shapes.front()->fillType();
}

Rect AppendShape::onGetBounds() const {
  Rect bounds = {};
  for (const auto& shape : shapes) {
    bounds.join(shape->onGetBounds());
  }
  return bounds;
}

Path AppendShape::onGetPath(float resolutionScale) const {
  auto firstShape = shapes.front();
  // the first path determines the fill type
  auto path = firstShape->onGetPath(resolutionScale);
  for (size_t i = 1; i < shapes.size(); ++i) {
    path.addPath(shapes[i]->onGetPath(resolutionScale));
  }
  return path;
}
}  // namespace tgfx
