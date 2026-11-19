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

bool MergeShape::unionConcatenates() const {
  if (pathOp != PathOp::Union) {
    return false;
  }
  // A text run reports its em box, which overlaps neighbours whose outlines do not, so both
  // fillType() and onGetPath() decide from the reported bounds: deciding from tighter outline
  // bounds here would concatenate where fillType() promised a boolean result. Reported rules are
  // not reliable either, so the outlines have to confirm them before any concatenation.
  auto firstType = first->fillType();
  auto secondType = second->fillType();
  if (!PathRef::UnionsByConcatenation(firstType, first->getBounds(), secondType,
                                      second->getBounds())) {
    return false;
  }
  auto firstPath = first->getPath();
  auto secondPath = second->getPath();
  return firstPath.getFillType() == firstType && secondPath.getFillType() == secondType &&
         PathRef::UnionsByConcatenation(firstPath.getFillType(), firstPath.getBounds(),
                                        secondPath.getFillType(), secondPath.getBounds());
}

PathFillType MergeShape::fillType() const {
  if (pathOp == PathOp::Append || pathOp == PathOp::Extend) {
    return getPath().getFillType();
  }
  if (unionConcatenates()) {
    // Concatenation keeps the first operand's outline unchanged, including its rule.
    return first->getPath().getFillType();
  }
  // A boolean operation resolves both operands under the rules of the outlines it receives and
  // writes an even-odd result, inverse only when the area outside that result stays filled.
  auto firstInverse = first->getPath().isInverseFillType();
  auto secondInverse = second->getPath().isInverseFillType();
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

// Deriving the flag from fillType() keeps the two public queries in agreement with the outline,
// including the empty result reported when a boolean operation fails.
bool MergeShape::isInverseFillType() const {
  return Shape::isInverseFillType();
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
  // fillType() and onGetPath() must agree on the fast path, so both use the shared decision.
  if (!PathRef::AddPath(path, secondPath, pathOp, unionConcatenates())) {
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
