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
#include "core/utils/MathExtra.h"

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

bool MergeShape::unionConcatenates(const Path& firstPath, const Path& secondPath) const {
  if (pathOp != PathOp::Union) {
    return false;
  }
  // A text run reports its em box, which overlaps neighbours whose outlines do not, so the fast
  // path is decided from the reported bounds. A Shape can also report a rule that differs from its
  // outline, and an outline grown by a non-unit resolution scale can reach a neighbour that the
  // scale=1 outline misses, so the two outlines actually being combined must confirm the rules
  // and disjointness before concatenation.
  auto firstType = first->fillType();
  auto secondType = second->fillType();
  if (!PathRef::UnionsByConcatenation(firstType, first->getBounds(), secondType,
                                      second->getBounds())) {
    return false;
  }
  return firstPath.getFillType() == firstType && secondPath.getFillType() == secondType &&
         PathRef::UnionsByConcatenation(firstPath.getFillType(), firstPath.getBounds(),
                                        secondPath.getFillType(), secondPath.getBounds());
}

// fillType() and the base isInverseFillType() both describe the rule of the outline handed out, so
// the inverse flag needs no separate override here.
PathFillType MergeShape::fillType() const {
  if (pathOp == PathOp::Append || pathOp == PathOp::Extend) {
    return getPath().getFillType();
  }
  auto firstPath = first->getPath();
  auto secondPath = second->getPath();
  if (unionConcatenates(firstPath, secondPath)) {
    // Concatenation keeps the first operand's outline unchanged, including its rule.
    return firstPath.getFillType();
  }
  // A boolean operation resolves both operands under the rules of the outlines it receives and
  // writes an even-odd result, inverse only when the area outside that result stays filled.
  auto firstInverse = firstPath.isInverseFillType();
  auto secondInverse = secondPath.isInverseFillType();
  bool inverse = false;
  switch (pathOp) {
    case PathOp::Union:
      inverse = firstInverse || secondInverse;
      break;
    case PathOp::Intersect:
      inverse = firstInverse && secondInverse;
      break;
    case PathOp::Difference:
      inverse = firstInverse && !secondInverse;
      break;
    case PathOp::XOR:
      inverse = firstInverse != secondInverse;
      break;
    default:
      break;
  }
  return inverse ? PathFillType::InverseEvenOdd : PathFillType::EvenOdd;
}

Rect MergeShape::onGetBounds() const {
  auto firstBounds = first->onGetBounds();
  auto secondBounds = second->onGetBounds();
  switch (pathOp) {
    case PathOp::Difference:
      return second->isInverseFillType() ? secondBounds : firstBounds;
    case PathOp::Intersect: {
      // Only these two operations need the inverse flags; asking for them on a union chain would
      // build every operand path just to discard the answer.
      auto firstInverse = first->isInverseFillType();
      auto secondInverse = second->isInverseFillType();
      if (firstInverse == secondInverse) {
        return firstBounds.intersect(secondBounds) ? firstBounds : Rect::MakeEmpty();
      }
      return firstInverse ? secondBounds : firstBounds;
    }
    default:
      firstBounds.join(secondBounds);
      return firstBounds;
  }
}

Path MergeShape::onGetPath(float resolutionScale) const {
  // Reusing the cached scale=1 paths keeps a chain of merges linear: each operand is built once
  // instead of once per ancestor. Export paths with another scale build their own outlines, and
  // those exact outlines are used for the concatenation check below.
  bool unitScale = FloatNearlyEqual(resolutionScale, 1.0f);
  auto path = unitScale ? first->getPath() : first->onGetPath(resolutionScale);
  auto secondPath = unitScale ? second->getPath() : second->onGetPath(resolutionScale);
  if (!PathRef::AddPath(path, secondPath, pathOp, unionConcatenates(path, secondPath))) {
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
