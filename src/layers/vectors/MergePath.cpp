/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/layers/vectors/MergePath.h"
#include "VectorContext.h"
#include "core/utils/Log.h"

namespace tgfx {

static PathOp ToPathOp(MergePathOp mode) {
  switch (mode) {
    case MergePathOp::Append:
      return PathOp::Append;
    case MergePathOp::Union:
      return PathOp::Union;
    case MergePathOp::Difference:
      return PathOp::Difference;
    case MergePathOp::Intersect:
      return PathOp::Intersect;
    case MergePathOp::XOR:
      return PathOp::XOR;
  }
  return PathOp::Append;
}

void MergePath::setMode(MergePathOp value) {
  if (_mode == value) {
    return;
  }
  _mode = value;
  invalidateContent();
}

void MergePath::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (context->geometries.empty()) {
    return;
  }
  auto geometries = context->getShapeGeometries();

  auto pathOp = ToPathOp(_mode);
  std::shared_ptr<Shape> mergedShape = nullptr;
  for (auto* geometry : geometries) {
    if (geometry->shape == nullptr) {
      continue;
    }
    auto shapeWithMatrix = Shape::ApplyMatrix(geometry->shape, geometry->matrix);
    mergedShape = Shape::Merge(mergedShape, shapeWithMatrix, pathOp);
  }

  context->geometries.clear();
  context->painters.clear();

  if (mergedShape) {
    context->addShape(std::move(mergedShape));
  }
}

}  // namespace tgfx
