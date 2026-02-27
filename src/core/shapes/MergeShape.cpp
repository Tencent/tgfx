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

PathFillType MergeShape::fillType() const {
  bool firstInverse = first->isInverseFillType();
  bool secondInverse = second->isInverseFillType();
  bool inverse = false;
  switch (pathOp) {
    case PathOp::Difference:
      inverse = firstInverse && !secondInverse;
      break;
    case PathOp::Intersect:
      inverse = firstInverse && secondInverse;
      break;
    case PathOp::Union:
      inverse = firstInverse || secondInverse;
      break;
    case PathOp::XOR:
      inverse = firstInverse != secondInverse;
      break;
    default:
      break;
  }
  // PathOp always produces EvenOdd fill type regardless of input fill types.
  return inverse ? PathFillType::InverseEvenOdd : PathFillType::EvenOdd;
}

Rect MergeShape::onGetBounds() const {
  auto firstBounds = first->onGetBounds();
  auto secondBounds = second->onGetBounds();
  bool firstInverse = first->isInverseFillType();
  bool secondInverse = second->isInverseFillType();
  switch (pathOp) {
    case PathOp::Difference:
      return secondInverse ? secondBounds : firstBounds;
    case PathOp::Intersect:
      if (firstInverse == secondInverse) {
        return firstBounds.intersect(secondBounds) ? firstBounds : Rect::MakeEmpty();
      }
      return firstInverse ? secondBounds : firstBounds;
    default:
      firstBounds.join(secondBounds);
      return firstBounds;
  }
}

Path MergeShape::onGetPath(float resolutionScale) const {
  auto path = first->onGetPath(resolutionScale);
  auto secondPath = second->onGetPath(resolutionScale);
  path.addPath(secondPath, pathOp);
  return path;
}

}  // namespace tgfx
