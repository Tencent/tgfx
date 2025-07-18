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

#include "StrokeShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/utils/ApplyStrokeToBounds.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

std::shared_ptr<Shape> Shape::ApplyStroke(std::shared_ptr<Shape> shape, const Stroke* stroke) {
  return StrokeShape::Apply(std::move(shape), stroke, true);
}

std::shared_ptr<Shape> StrokeShape::Apply(std::shared_ptr<Shape> shape, const Stroke* stroke,
                                          bool useOwnUniqueKey) {
  if (shape == nullptr) {
    return nullptr;
  }
  if (stroke == nullptr) {
    return shape;
  }
  if (stroke->width <= 0.0f) {
    return nullptr;
  }
  if (shape->type() != Type::Matrix) {
    return std::shared_ptr<StrokeShape>(
        new StrokeShape(std::move(shape), *stroke, useOwnUniqueKey));
  }
  // Always apply stroke to the shape before the matrix, so that the outer matrix can be used to
  // do some optimization.
  auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
  auto scales = matrixShape->matrix.getAxisScales();
  if (scales.x != scales.y) {
    return std::shared_ptr<StrokeShape>(
        new StrokeShape(std::move(shape), *stroke, useOwnUniqueKey));
  }
  auto scaleStroke = *stroke;
  DEBUG_ASSERT(scales.x != 0);
  scaleStroke.width /= scales.x;
  shape = std::shared_ptr<StrokeShape>(
      new StrokeShape(matrixShape->shape, scaleStroke, useOwnUniqueKey));
  return std::make_shared<MatrixShape>(std::move(shape), matrixShape->matrix);
}

Rect StrokeShape::getBounds() const {
  auto bounds = shape->getBounds();
  ApplyStrokeToBounds(stroke, &bounds, true);
  return bounds;
}

Path StrokeShape::getPath() const {
  auto path = shape->getPath();
  stroke.applyToPath(&path);
  return path;
}

UniqueKey StrokeShape::getUniqueKey() const {
  if (useOwnUniqueKey) {
    return uniqueKey.get();
  }
  static const auto WidthStrokeShapeType = UniqueID::Next();
  static const auto CapJoinStrokeShapeType = UniqueID::Next();
  static const auto FullStrokeShapeType = UniqueID::Next();
  auto hasMiter = stroke.join == LineJoin::Miter && stroke.miterLimit != 4.0f;
  auto hasCapJoin = (hasMiter || stroke.cap != LineCap::Butt || stroke.join != LineJoin::Miter);
  size_t count = 2 + (hasCapJoin ? 1 : 0) + (hasMiter ? 1 : 0);
  auto type =
      hasCapJoin ? (hasMiter ? FullStrokeShapeType : CapJoinStrokeShapeType) : WidthStrokeShapeType;
  BytesKey bytesKey(count);
  bytesKey.write(type);
  bytesKey.write(stroke.width);
  if (hasCapJoin) {
    bytesKey.write(static_cast<uint32_t>(stroke.join) << 16 | static_cast<uint32_t>(stroke.cap));
  }
  if (hasMiter) {
    bytesKey.write(stroke.miterLimit);
  }
  return UniqueKey::Append(shape->getUniqueKey(), bytesKey.data(), bytesKey.size());
}
}  // namespace tgfx
