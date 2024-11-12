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

#include "tgfx/core/Shape.h"
#include "core/GlyphRunList.h"
#include "core/shapes/AppendShape.h"
#include "core/shapes/EffectShape.h"
#include "core/shapes/GlyphShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/MergeShape.h"
#include "core/shapes/PathShape.h"
#include "core/shapes/StrokeShape.h"

namespace tgfx {
std::shared_ptr<Shape> Shape::MakeFrom(Path path) {
  if (path.isEmpty()) {
    return nullptr;
  }
  return std::make_shared<PathShape>(std::move(path));
}

std::shared_ptr<Shape> Shape::MakeFrom(std::shared_ptr<TextBlob> textBlob) {
  auto glyphRunLists = GlyphRunList::Unwrap(textBlob.get());
  if (glyphRunLists == nullptr || glyphRunLists->size() != 1) {
    return nullptr;
  }
  auto glyphRunList = (*glyphRunLists)[0];
  if (glyphRunList->hasColor() || !glyphRunList->hasOutlines()) {
    return nullptr;
  }
  return std::make_shared<GlyphShape>(std::move(glyphRunList));
}

void Shape::Append(std::vector<std::shared_ptr<Shape>>* shapes, std::shared_ptr<Shape> shape) {
  if (shape->type() == Type::Append) {
    auto appendShape = std::static_pointer_cast<AppendShape>(shape);
    shapes->insert(shapes->end(), appendShape->shapes.end(), appendShape->shapes.begin());
  } else {
    shapes->push_back(std::move(shape));
  }
}

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
    std::vector<std::shared_ptr<Shape>> shapes = {};
    Append(&shapes, std::move(first));
    Append(&shapes, std::move(second));
    return std::make_shared<AppendShape>(std::move(shapes));
  }
  return std::make_shared<MergeShape>(std::move(first), std::move(second), pathOp);
}

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
  if (scales.x != scales.y) {
    return std::make_shared<StrokeShape>(std::move(shape), *stroke);
  }
  auto scaleStroke = *stroke;
  scaleStroke.width *= scales.x;
  shape = std::make_shared<StrokeShape>(matrixShape->shape, scaleStroke);
  return std::make_shared<MatrixShape>(std::move(shape), matrixShape->matrix);
}

std::shared_ptr<Shape> Shape::ApplyMatrix(std::shared_ptr<Shape> shape, const Matrix& matrix) {
  if (shape == nullptr) {
    return nullptr;
  }
  if (matrix.isIdentity()) {
    return shape;
  }
  auto maxScale = matrix.getMaxScale();
  if (maxScale <= 0) {
    return nullptr;
  }
  if (shape->type() != Type::Matrix) {
    return std::make_shared<MatrixShape>(std::move(shape), matrix);
  }
  auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
  auto totalMatrix = matrix * matrixShape->matrix;
  if (totalMatrix.isIdentity()) {
    return matrixShape->shape;
  }
  return std::make_shared<MatrixShape>(matrixShape->shape, totalMatrix);
}

std::shared_ptr<Shape> Shape::ApplyEffect(std::shared_ptr<Shape> shape,
                                          std::shared_ptr<PathEffect> effect) {

  if (shape == nullptr) {
    return nullptr;
  }
  if (effect == nullptr) {
    return shape;
  }
  return std::make_shared<EffectShape>(std::move(shape), std::move(effect));
}

Rect Shape::getBounds(float) const {
  return Rect::MakeEmpty();
}

Path Shape::getPath(float) const {
  return {};
}

bool Shape::isLine(Point*) const {
  return false;
}

bool Shape::isRect(Rect*) const {
  return false;
}

bool Shape::isOval(Rect*) const {
  return false;
}

bool Shape::isRRect(RRect*) const {
  return false;
}

}  // namespace tgfx