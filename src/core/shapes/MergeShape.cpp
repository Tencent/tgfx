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
#include "PathShape.h"

namespace tgfx {
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

Rect MergeShape::getBounds(float resolutionScale) const {
  auto bounds = first->getBounds(resolutionScale);
  if (pathOp == PathOp::Difference) {
    return bounds;
  }
  auto secondBounds = second->getBounds(resolutionScale);
  if (pathOp == PathOp::Intersect) {
    if (!bounds.intersects(secondBounds)) {
      return Rect::MakeEmpty();
    }
    return bounds;
  }
  bounds.join(secondBounds);
  return bounds;
}

Path MergeShape::getPath(float resolutionScale) const {
  auto path = first->getPath(resolutionScale);
  auto secondPath = second->getPath(resolutionScale);
  path.addPath(secondPath, pathOp);
  return path;
}

}  // namespace tgfx
