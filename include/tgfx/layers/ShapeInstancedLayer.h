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

#pragma once

#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeStyle.h"

namespace tgfx {
/**
 * ShapeInstancedLayer is a layer that draws multiple instances of the same shape with different
 * transformations and optional per-instance colors. All instances share the same geometry, enabling
 * efficient batched rendering via GPU instancing.
 */
class ShapeInstancedLayer : public Layer {
 public:
  /**
   * Creates a new shape instanced layer instance.
   */
  static std::shared_ptr<ShapeInstancedLayer> Make();

  LayerType type() const override {
    return LayerType::ShapeInstanced;
  }

  /**
   * Returns the Shape object shared by all instances.
   */
  std::shared_ptr<Shape> shape() const {
    return _shape;
  }

  /**
   * Sets the Shape object shared by all instances.
   */
  void setShape(std::shared_ptr<Shape> shape);

  /**
   * Returns the transformation matrices for all instances.
   */
  const std::vector<Matrix>& matrices() const {
    return _matrices;
  }

  /**
   * Sets the transformation matrices for all instances. Each matrix defines the position, rotation,
   * and scale of one instance.
   */
  void setMatrices(std::vector<Matrix> matrices);

  /**
   * Returns the per-instance colors. An empty vector means no per-instance colors are used.
   */
  const std::vector<Color>& colors() const {
    return _colors;
  }

  /**
   * Sets the per-instance colors. If provided, the vector must have the same number of elements as
   * matrices. Pass an empty vector to use paint color or shader color only.
   */
  void setColors(std::vector<Color> colors);

  /**
   * Returns the list of fill styles used to fill the instances. Each style contains a shader, alpha,
   * and blend mode. Color blending follows the same rules as drawMesh():
   * - If per-instance colors are set and a style has a shader: texture color * instance color.
   * - If per-instance colors are not set and a style has a shader: pure texture color.
   * - If per-instance colors are set and a style has no shader: instance color.
   * - If per-instance colors are not set and a style has no shader: style color.
   * By default, the fill styles list is empty.
   */
  const std::vector<std::shared_ptr<ShapeStyle>>& fillStyles() const {
    return _fillStyles;
  }

  /**
   * Replaces the current list of fill styles with the given list.
   */
  void setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills);

  /**
   * Removes all fill styles from the layer.
   */
  void removeFillStyles();

  /**
   * Replaces the current list of fill styles with the given fill style.
   */
  void setFillStyle(std::shared_ptr<ShapeStyle> fillStyle);

  /**
   * Adds a fill style to the end of the existing fill styles.
   */
  void addFillStyle(std::shared_ptr<ShapeStyle> fillStyle);

 protected:
  ShapeInstancedLayer() = default;

  void onUpdateContent(LayerRecorder* recorder) override;

 private:
  std::shared_ptr<Shape> _shape = nullptr;
  std::vector<Matrix> _matrices = {};
  std::vector<Color> _colors = {};
  std::vector<std::shared_ptr<ShapeStyle>> _fillStyles = {};
};
}  // namespace tgfx
