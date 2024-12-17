/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

namespace tgfx {
void Shape::Append(std::vector<std::shared_ptr<Shape>>* shapes, std::shared_ptr<Shape> shape) {
  if (shape->type() == Type::Append) {
    auto appendShape = std::static_pointer_cast<AppendShape>(shape);
    shapes->insert(shapes->end(), appendShape->shapes.end(), appendShape->shapes.begin());
  } else {
    shapes->push_back(std::move(shape));
  }
}

bool AppendShape::isInverseFillType() const {
  return shapes.front()->isInverseFillType();
}

Rect AppendShape::getBounds(float resolutionScale) const {
  auto bounds = Rect::MakeEmpty();
  for (const auto& shape : shapes) {
    bounds.join(shape->getBounds(resolutionScale));
  }
  return bounds;
}

Path AppendShape::getPath(float resolutionScale) const {
  auto firstShape = shapes.front();
  // the first path determines the fill type
  auto path = firstShape->getPath(resolutionScale);
  for (size_t i = 1; i < shapes.size(); ++i) {
    path.addPath(shapes[i]->getPath(resolutionScale));
  }
  return path;
}
}  // namespace tgfx
