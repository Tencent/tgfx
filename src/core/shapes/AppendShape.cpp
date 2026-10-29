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
    if (shape == nullptr) {
      continue;
    }
    AppendShape::Append(&list, shape);
  }
  if (list.empty()) {
    return nullptr;
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
  // Rebuilding an outline the caller has not asked for yet is what this must avoid, so it stops at
  // the first source that contributes anything: one that has a contour, or an empty one that fills
  // the whole plane. The sources before it are the empty ones, whose paths are cheap to ask for;
  // the ones after it are never touched. Anything already built stays in the Shape cache, so a
  // later call reads that rather than rebuilding it.
  auto rule = PathFillType::Winding;
  for (const auto& shape : shapes) {
    auto path = shape->getPath();
    rule = path.getFillType();
    // An empty inverse path contributes the whole plane, so it is the one that sets the rule rather
    // than one of the sources that follow it.
    if (!path.isEmpty() || path.isInverseFillType()) {
      break;
    }
  }
  return rule;
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
  // Append retains the current rule, except that an empty non-inverse path is replaced by the next
  // operand. An empty inverse one fills the whole plane, so the operands are added to it instead.
  auto path = firstShape->onGetPath(resolutionScale);
  for (size_t i = 1; i < shapes.size(); ++i) {
    path.addPath(shapes[i]->onGetPath(resolutionScale));
  }
  return path;
}
}  // namespace tgfx
