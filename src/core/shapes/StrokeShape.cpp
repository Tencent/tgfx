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
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
#include "core/utils/UniqueID.h"
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

std::shared_ptr<Shape> Shape::ApplyStroke(std::shared_ptr<Shape> shape, const Stroke* stroke) {
  if (shape == nullptr) {
    return nullptr;
  }
  if (stroke == nullptr) {
    return shape;
  }
  if (shape->type() != Type::Matrix) {
    return std::make_shared<StrokeShape>(std::move(shape), *stroke);
  }
  // Always apply stroke to the shape before the matrix, so that the outer matrix can be used to
  // do some optimization.
  auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
  auto scales = matrixShape->matrix.getAxisScales();
  if (scales.x != scales.y || scales.x > 1.f) {
    return std::make_shared<StrokeShape>(std::move(shape), *stroke);
  }
  auto scaleStroke = *stroke;
  DEBUG_ASSERT(scales.x != 0);
  scaleStroke.width /= scales.x;
  shape = std::make_shared<StrokeShape>(matrixShape->shape, scaleStroke);
  return std::make_shared<MatrixShape>(std::move(shape), matrixShape->matrix);
}

Rect StrokeShape::onGetBounds() const {
  auto bounds = shape->onGetBounds();
  ApplyStrokeToBounds(stroke, &bounds, true);
  return bounds;
}

Path StrokeShape::onGetPath(float resolutionScale) const {
  if (FloatNearlyZero(resolutionScale)) {
    return {};
  }
  auto path = shape->onGetPath(resolutionScale);
  if (TreatStrokeAsHairline(stroke, Matrix::MakeScale(resolutionScale, resolutionScale))) {
    Stroke hairlineStroke = stroke;
    // When zoomed in by a matrix shape, reduce the stroke width ahead of time
    hairlineStroke.width = 1.f / resolutionScale;
    hairlineStroke.applyToPath(&path, resolutionScale);
    return path;
  }
  stroke.applyToPath(&path, resolutionScale);
  return path;
}

UniqueKey StrokeShape::MakeUniqueKey(const UniqueKey& key, const Stroke& stroke) {
  static const auto WidthStrokeShapeType = UniqueID::Next();
  static const auto CapJoinStrokeShapeType = UniqueID::Next();
  static const auto FullStrokeShapeType = UniqueID::Next();
  if (!IsHairlineStroke(stroke)) {
    auto hasMiter = stroke.join == LineJoin::Miter && stroke.miterLimit != 4.0f;
    auto hasCapJoin = hasMiter || stroke.cap != LineCap::Butt || stroke.join != LineJoin::Miter;
    size_t count = 2 + (hasCapJoin ? 1 : 0) + (hasMiter ? 1 : 0);
    auto type = hasCapJoin ? (hasMiter ? FullStrokeShapeType : CapJoinStrokeShapeType)
                           : WidthStrokeShapeType;
    BytesKey bytesKey(count);
    bytesKey.write(type);
    bytesKey.write(stroke.width);
    if (hasCapJoin) {
      bytesKey.write(static_cast<uint32_t>(stroke.join) << 16 | static_cast<uint32_t>(stroke.cap));
    }
    if (hasMiter) {
      bytesKey.write(stroke.miterLimit);
    }
    return UniqueKey::Append(key, bytesKey.data(), bytesKey.size());
  }
  // hairline stroke ignore cap, join and miterLimit,and width is always 0.f,so just use a fixed key.
  static const auto HairlineStrokeKey = []() -> BytesKey {
    auto hairlineStrokeType = UniqueID::Next();
    BytesKey bytesKey(1);
    bytesKey.write(hairlineStrokeType);
    return bytesKey;
  }();
  return UniqueKey::Append(key, HairlineStrokeKey.data(), HairlineStrokeKey.size());
}

UniqueKey StrokeShape::getUniqueKey() const {
  return MakeUniqueKey(shape->getUniqueKey(), stroke);
}
}  // namespace tgfx
