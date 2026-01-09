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

#pragma once

#include <memory>
#include <optional>
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Rect.h"
#include "tgfx/layers/filters/LayerFilter.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

/**
 * Defines how Matrix3DRegionTransformer should combine with outer Matrix3DRegionTransformer.
 */
enum class Matrix3DCombineMode {
  /**
   * Allow combining with outer Matrix3DRegionTransformer.
   * Used for layers inside 3D context to maintain 3D state.
   */
  Combinable,

  /**
   * Do not combine with outer Matrix3DRegionTransformer.
   * Used for 3D layers outside 3D context to avoid incorrect merging.
   */
  Isolated
};

/**
 * The RegionTransformer class is used to transform a rectangle region.
 */
class RegionTransformer {
 public:
  /**
   * Creates a RegionTransformer that clips the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromClip(
      const Rect& clipRect, std::shared_ptr<RegionTransformer> outer = nullptr);

  /**
   * Creates a RegionTransformer that applies filter transformations to the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromFilters(
      const std::vector<std::shared_ptr<LayerFilter>>& filters, float contentScale,
      std::shared_ptr<RegionTransformer> outer = nullptr);

  /**
   * Creates a RegionTransformer that applies style transformations to the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromStyles(
      const std::vector<std::shared_ptr<LayerStyle>>& styles, float contentScale,
      std::shared_ptr<RegionTransformer> outer = nullptr);

  /**
   * Creates a RegionTransformer that applies the given matrix transformation to the given rectangle.
   */
  static std::shared_ptr<RegionTransformer> MakeFromMatrix(
      const Matrix& matrix, std::shared_ptr<RegionTransformer> outer = nullptr);

  /**
   * Creates a RegionTransformer that applies the given 3D matrix transformation to the given
   * rectangle.
   * @param matrix The 3D transformation matrix to apply.
   * @param outer The outer RegionTransformer to chain with.
   * @param combineMode Controls whether this transformer can combine with outer Matrix3D transformers.
   */
  static std::shared_ptr<RegionTransformer> MakeFromMatrix3D(
      const Matrix3D& matrix, std::shared_ptr<RegionTransformer> outer = nullptr,
      Matrix3DCombineMode combineMode = Matrix3DCombineMode::Combinable);

  explicit RegionTransformer(std::shared_ptr<RegionTransformer> outer);

  virtual ~RegionTransformer() = default;

  /**
   * Transforms the given rectangle using the transformation defined by this object.
   */
  void transform(Rect* bounds) const;

  float getMaxScale() const;

  /**
   * Returns the accumulated matrix from consecutive Matrix3DRegionTransformers.
   * Returns nullopt if this transformer is not a Matrix3DRegionTransformer.
   */
  std::optional<Matrix3D> getConsecutiveMatrix3D() const;

 protected:
  virtual void onTransform(Rect* bounds) const = 0;

  virtual bool isClip() const {
    return false;
  }

  virtual bool isMatrix() const {
    return false;
  }

  virtual bool isMatrix3D() const {
    return false;
  }

  void getTotalMatrix(Matrix* matrix) const;

 private:
  std::shared_ptr<RegionTransformer> outer = nullptr;
};
}  // namespace tgfx
