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

#include "MergeShape.h"
#include "core/shapes/AppendShape.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::Merge(std::shared_ptr<Shape> first, std::shared_ptr<Shape> second,
                                    PathOp pathOp) {
  if (first == nullptr) {
    if (second == nullptr) {
      return nullptr;
    }
    return second;
  }
  if (second == nullptr) {
    return first;
  }
  if (pathOp == PathOp::Append) {
    return AppendShape::MakeFrom(std::move(first), std::move(second));
  }
  return std::make_shared<MergeShape>(std::move(first), std::move(second), pathOp);
}

bool MergeShape::isRect(Rect* rect) const {
  if (pathOp != PathOp::Intersect) {
    return false;
  }
  Rect firstRect = {};
  if (!first->isRect(&firstRect)) {
    return false;
  }
  Rect secondRect = {};
  if (!second->isRect(&secondRect)) {
    return false;
  }
  if (rect) {
    if (!firstRect.intersect(secondRect)) {
      return false;
    }
    *rect = firstRect;
  }
  return true;
}

bool MergeShape::isInverseFillType() const {
  switch (pathOp) {
    case PathOp::Difference:
      return first->isInverseFillType() && !second->isInverseFillType();
    case PathOp::Intersect:
      return first->isInverseFillType() && second->isInverseFillType();
    case PathOp::Union:
      return first->isInverseFillType() || second->isInverseFillType();
    case PathOp::XOR:
      return first->isInverseFillType() != second->isInverseFillType();
    default:
      return false;
  }
}

Rect MergeShape::getBounds(float resolutionScale) const {
  auto firstBounds = first->getBounds(resolutionScale);
  auto secondBounds = second->getBounds(resolutionScale);
  switch (pathOp) {
    case PathOp::Difference:
      return second->isInverseFillType() ? secondBounds : firstBounds;
    case PathOp::Intersect:
      if (first->isInverseFillType() == second->isInverseFillType()) {
        return firstBounds.intersects(secondBounds) ? firstBounds : Rect::MakeEmpty();
      }
      return first->isInverseFillType() ? secondBounds : firstBounds;
    default:
      firstBounds.join(secondBounds);
      return firstBounds;
  }
}

Path MergeShape::getPath(float resolutionScale) const {
  auto path = first->getPath(resolutionScale);
  auto secondPath = second->getPath(resolutionScale);
  path.addPath(secondPath, pathOp);
  return path;
}

}  // namespace tgfx
