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
#include "core/PathRef.h"
#include "core/shapes/AppendShape.h"
#include "core/utils/Log.h"

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
  if (pathOp == PathOp::Append || pathOp == PathOp::Extend) {
    return getPath().getFillType();
  }
  if (pathOp == PathOp::Union) {
    auto firstType = first->fillType();
    if (PathRef::UnionsByConcatenation(firstType, first->getBounds(), second->fillType(),
                                       second->getBounds())) {
      return firstType;
    }
  }
  return isInverseFillType() ? PathFillType::InverseEvenOdd : PathFillType::EvenOdd;
}

bool MergeShape::isInverseFillType() const {
  switch (pathOp) {
    case PathOp::Union:
      return first->isInverseFillType() || second->isInverseFillType();
    case PathOp::Intersect:
      return first->isInverseFillType() && second->isInverseFillType();
    case PathOp::Difference:
      return first->isInverseFillType() && !second->isInverseFillType();
    case PathOp::XOR:
      return first->isInverseFillType() != second->isInverseFillType();
    default:
      return getPath().isInverseFillType();
  }
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
  // fillType() decides a union from the bounds the two shapes report, and those bounds may be larger
  // than the outlines they contain - a text run reports its em box, whose neighbours nearly always
  // overlap while the glyph outlines do not. Deciding here from the tighter outline bounds would
  // concatenate where fillType() promised a boolean result, leaving the shape reporting one rule and
  // handing out an outline read by another, so the same bounds decide it in both places.
  if (!PathRef::AddPath(path, secondPath, pathOp, first->getBounds(), second->getBounds())) {
    // A failed operation leaves the first operand behind, which is not the merge that was asked
    // for: reading it as the result would draw one side of the operation as if it were both. An
    // empty path is the only answer that is wrong in a visible way rather than a silent one, and
    // it still carries the rule fillType() reports, so nothing downstream reads a mixed result.
    LOGE("MergeShape::onGetPath() Failed to merge the two shapes; reporting an empty result.");
    path = {};
    path.setFillType(fillType());
  }
  return path;
}

}  // namespace tgfx
