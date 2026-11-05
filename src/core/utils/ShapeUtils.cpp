/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "ShapeUtils.h"
#include <algorithm>
#include "core/shapes/EffectShape.h"
#include "core/shapes/Matrix3DShape.h"
#include "core/shapes/MatrixShape.h"
#include "core/shapes/ReverseShape.h"
#include "core/shapes/StrokeShape.h"
#include "core/shapes/TextShape.h"
#include "core/utils/Log.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

Path ShapeUtils::GetShapeRenderingPath(std::shared_ptr<Shape> shape, float resolutionScale) {
  if (shape == nullptr) {
    return {};
  }
  return shape->onGetPath(resolutionScale);
}

bool ShapeUtils::GetPathSources(const std::shared_ptr<Shape>& shape, float resolutionScale,
                                std::vector<Path>* paths) {
  if (shape == nullptr || paths == nullptr) {
    return false;
  }
  switch (shape->type()) {
    case Shape::Type::Text:
      return std::static_pointer_cast<TextShape>(shape)->getGlyphPaths(resolutionScale, paths);
    case Shape::Type::Matrix: {
      auto wrapper = std::static_pointer_cast<MatrixShape>(shape);
      if (!GetPathSources(wrapper->shape, resolutionScale * wrapper->matrix.getMaxScale(), paths)) {
        return false;
      }
      for (auto& path : *paths) {
        path.transform(wrapper->matrix);
      }
      return true;
    }
    case Shape::Type::Matrix3D: {
      auto wrapper = std::static_pointer_cast<Matrix3DShape>(shape);
      if (!GetPathSources(wrapper->shape, resolutionScale, paths)) {
        return false;
      }
      for (auto& path : *paths) {
        path.transform3D(wrapper->matrix);
      }
      return true;
    }
    case Shape::Type::Effect: {
      auto wrapper = std::static_pointer_cast<EffectShape>(shape);
      if (!GetPathSources(wrapper->shape, resolutionScale, paths)) {
        return false;
      }
      ApplyEffectToSources(*wrapper->effect, paths);
      return true;
    }
    case Shape::Type::Reverse: {
      auto wrapper = std::static_pointer_cast<ReverseShape>(shape);
      if (!GetPathSources(wrapper->shape, resolutionScale, paths)) {
        return false;
      }
      std::reverse(paths->begin(), paths->end());
      for (auto& path : *paths) {
        path.reverse();
      }
      return true;
    }
    default:
      // FillType reinterprets the whole combined path. It and explicit combinations establish a
      // source boundary before subsequent effects, regardless of whether the result is stroked.
      *paths = {shape->onGetPath(resolutionScale)};
      return true;
  }
}

bool ShapeUtils::ApplyEffectToSources(const PathEffect& effect, std::vector<Path>* paths) {
  if (paths == nullptr) {
    return false;
  }
  auto result = *paths;
  if (!effect.filterPaths(&result)) {
    LOGE("ShapeUtils: path effect rejected the sources; keeping the original contours.");
    return false;
  }
  *paths = std::move(result);
  return true;
}

Path ShapeUtils::GetContourPath(const std::shared_ptr<Shape>& shape, float resolutionScale) {
  std::vector<Path> paths = {};
  if (!GetPathSources(shape, resolutionScale, &paths)) {
    return {};
  }
  Path contours = {};
  for (const auto& path : paths) {
    contours.addPath(path);
  }
  return contours;
}

float ShapeUtils::CalculateAlphaReduceFactorIfHairline(std::shared_ptr<Shape> shape) {
  auto [strokeShape, matrix] = DecomposeStrokeShape(shape);
  if (!strokeShape || strokeShape->stroke.width <= 0.f) {
    return 1.f;
  }
  float scale = matrix.getMaxScale();
  return std::min(strokeShape->stroke.width * scale, 1.f);
}

std::tuple<std::shared_ptr<StrokeShape>, Matrix> ShapeUtils::DecomposeStrokeShape(
    std::shared_ptr<Shape> shape) {
  std::shared_ptr<StrokeShape> strokeShape = nullptr;
  Matrix matrix = Matrix::I();

  if (!shape) {
    return {strokeShape, matrix};
  }

  if (shape->type() == Shape::Type::Matrix) {
    auto matrixShape = std::static_pointer_cast<MatrixShape>(shape);
    if (matrixShape->shape->type() == Shape::Type::Stroke) {
      strokeShape = std::static_pointer_cast<StrokeShape>(matrixShape->shape);
      matrix = matrixShape->matrix;
    }
  } else if (shape->type() == Shape::Type::Stroke) {
    strokeShape = std::static_pointer_cast<StrokeShape>(shape);
  }

  return {strokeShape, matrix};
}

const MatrixShape* ShapeUtils::AsMatrixShape(const Shape* shape) {
  if (shape != nullptr && shape->type() == Shape::Type::Matrix) {
    return static_cast<const MatrixShape*>(shape);
  }
  return nullptr;
}

}  // namespace tgfx
