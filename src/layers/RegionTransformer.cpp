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

#include "RegionTransformer.h"
#include "core/utils/Log.h"

namespace tgfx {
class ClipRegionTransformer : public RegionTransformer {
 public:
  ClipRegionTransformer(const Rect& clipRect, std::shared_ptr<RegionTransformer> outer)
      : RegionTransformer(std::move(outer)), clipRect(clipRect) {
  }

  Rect clipRect;

 protected:
  void onTransform(Rect* bounds) const override {
    if (!bounds->intersect(clipRect)) {
      bounds->setEmpty();
    }
  }

  bool isClip() const override {
    return true;
  }
};

class FilterRegionTransformer : public RegionTransformer {
 public:
  FilterRegionTransformer(const std::vector<std::shared_ptr<LayerFilter>>& filters,
                          float contentScale, std::shared_ptr<RegionTransformer> outer)
      : RegionTransformer(std::move(outer)), filters(filters), contentScale(contentScale) {
  }

 protected:
  void onTransform(Rect* bounds) const override {
    for (auto& filter : filters) {
      *bounds = filter->filterBounds(*bounds, contentScale);
    }
  }

 private:
  const std::vector<std::shared_ptr<LayerFilter>>& filters;
  float contentScale;
};

class StyleRegionTransformer : public RegionTransformer {
 public:
  StyleRegionTransformer(const std::vector<std::shared_ptr<LayerStyle>>& styles, float contentScale,
                         std::shared_ptr<RegionTransformer> outer)
      : RegionTransformer(std::move(outer)), styles(styles), contentScale(contentScale) {
  }

 protected:
  void onTransform(Rect* bounds) const override {
    auto layerBounds = *bounds;
    for (auto& style : styles) {
      auto styleBounds = style->filterBounds(layerBounds, contentScale);
      bounds->join(styleBounds);
    }
  }

 private:
  const std::vector<std::shared_ptr<LayerStyle>>& styles;
  float contentScale;
};

class MatrixRegionTransformer : public RegionTransformer {
 public:
  MatrixRegionTransformer(const Matrix& matrix, std::shared_ptr<RegionTransformer> outer)
      : RegionTransformer(std::move(outer)), matrix(matrix) {
  }

  Matrix matrix;

 protected:
  void onTransform(Rect* bounds) const override {
    *bounds = matrix.mapRect(*bounds);
  }

  bool isMatrix() const override {
    return true;
  }
};

class Matrix3DRegionTransformer : public RegionTransformer {
 public:
  Matrix3DRegionTransformer(const Matrix3D& matrix, std::shared_ptr<RegionTransformer> outer)
      : RegionTransformer(std::move(outer)), matrix(matrix) {
  }

  Matrix3D matrix;

 protected:
  void onTransform(Rect* bounds) const override {
    *bounds = matrix.mapRect(*bounds);
  }

  bool isMatrix3D() const override {
    return true;
  }
};

std::shared_ptr<RegionTransformer> RegionTransformer::MakeFromClip(
    const Rect& clipRect, std::shared_ptr<RegionTransformer> outer) {
  if (!outer || !outer->isClip()) {
    return std::make_unique<ClipRegionTransformer>(clipRect, std::move(outer));
  }
  auto outClipRect = static_cast<ClipRegionTransformer*>(outer.get())->clipRect;
  if (!outClipRect.intersect(clipRect)) {
    outClipRect = {};
  }
  return std::make_unique<ClipRegionTransformer>(outClipRect, outer->outer);
}

std::shared_ptr<RegionTransformer> RegionTransformer::MakeFromFilters(
    const std::vector<std::shared_ptr<LayerFilter>>& filters, float contentScale,
    std::shared_ptr<RegionTransformer> outer) {
  if (filters.empty()) {
    return outer;
  }
  return std::make_shared<FilterRegionTransformer>(filters, contentScale, std::move(outer));
}

std::shared_ptr<RegionTransformer> RegionTransformer::MakeFromStyles(
    const std::vector<std::shared_ptr<LayerStyle>>& styles, float contentScale,
    std::shared_ptr<RegionTransformer> outer) {
  if (styles.empty()) {
    return outer;
  }
  return std::make_shared<StyleRegionTransformer>(styles, contentScale, std::move(outer));
}

std::shared_ptr<RegionTransformer> RegionTransformer::MakeFromMatrix(
    const Matrix& matrix, std::shared_ptr<RegionTransformer> outer) {
  if (matrix.isIdentity()) {
    return outer;
  }
  if (!outer || !outer->isMatrix()) {
    return std::make_shared<MatrixRegionTransformer>(matrix, std::move(outer));
  }

  auto combineMatrix = matrix;
  combineMatrix.postConcat(static_cast<MatrixRegionTransformer*>(outer.get())->matrix);
  return std::make_shared<MatrixRegionTransformer>(combineMatrix, outer->outer);
}

std::shared_ptr<RegionTransformer> RegionTransformer::MakeFromMatrix3D(
    const Matrix3D& matrix, std::shared_ptr<RegionTransformer> outer) {
  if (matrix == Matrix3D::I()) {
    return outer;
  }
  if (!outer || !outer->isMatrix3D()) {
    return std::make_shared<Matrix3DRegionTransformer>(matrix, std::move(outer));
  }

  auto combineMatrix = matrix;
  combineMatrix.postConcat(static_cast<Matrix3DRegionTransformer*>(outer.get())->matrix);
  return std::make_shared<Matrix3DRegionTransformer>(combineMatrix, outer->outer);
}

RegionTransformer::RegionTransformer(std::shared_ptr<RegionTransformer> outer)
    : outer(std::move(outer)) {
}

void RegionTransformer::transform(Rect* bounds) const {
  DEBUG_ASSERT(bounds != nullptr);
  onTransform(bounds);
  if (outer) {
    outer->transform(bounds);
  }
}

float RegionTransformer::getMaxScale() const {
  Matrix matrix = Matrix::I();
  getTotalMatrix(&matrix);
  return matrix.getMaxScale();
}

void RegionTransformer::getTotalMatrix(Matrix* matrix) const {
  DEBUG_ASSERT(matrix != nullptr);
  if (isMatrix()) {
    matrix->postConcat(static_cast<const MatrixRegionTransformer*>(this)->matrix);
  }
  if (outer) {
    outer->getTotalMatrix(matrix);
  }
}

std::optional<Matrix3D> RegionTransformer::getConsecutiveMatrix3D() const {
  if (!isMatrix3D()) {
    return std::nullopt;
  }
  auto result = static_cast<const Matrix3DRegionTransformer*>(this)->matrix;
  auto current = outer;
  while (current && current->isMatrix3D()) {
    result.postConcat(static_cast<const Matrix3DRegionTransformer*>(current.get())->matrix);
    current = current->outer;
  }
  return result;
}

}  // namespace tgfx
