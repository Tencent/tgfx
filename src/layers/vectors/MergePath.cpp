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

void MergePath::setPathOp(PathOp value) {
  if (_pathOp == value) {
    return;
  }
  _pathOp = value;
  invalidateContent();
}

void MergePath::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (context->geometries.empty()) {
    return;
  }
  auto geometries = context->getShapeGeometries();

  std::shared_ptr<Shape> mergedShape = nullptr;
  for (size_t i = 0; i < geometries.size(); i++) {
    auto& shape = geometries[i]->shape;
    if (shape == nullptr) {
      continue;
    }
    auto shapeWithMatrix = Shape::ApplyMatrix(shape, context->matrices[i]);
    mergedShape = Shape::Merge(mergedShape, shapeWithMatrix, _pathOp);
  }

  context->geometries.clear();
  context->matrices.clear();
  context->painters.clear();

  if (mergedShape) {
    context->addShape(std::move(mergedShape));
  }
}

}  // namespace tgfx
