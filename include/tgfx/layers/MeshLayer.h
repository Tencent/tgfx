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

#include "tgfx/core/Mesh.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeStyle.h"

namespace tgfx {
/**
 * MeshLayer is a layer that draws a mesh with vertex colors or textures. Unlike ShapeLayer,
 * MeshLayer does not support stroke styles since meshes are rendered as filled triangles.
 *
 * Note: Hit testing for MeshLayer uses bounding box only, not precise triangle intersection.
 * This may produce false positives for concave or sparse meshes where a point lies inside the
 * bounding box but outside all triangles.
 */
class MeshLayer : public Layer {
 public:
  /**
   * Creates a new mesh layer instance.
   */
  static std::shared_ptr<MeshLayer> Make();

  LayerType type() const override {
    return LayerType::Mesh;
  }

  /**
   * Returns the Mesh object defining the mesh to be rendered.
   */
  std::shared_ptr<Mesh> mesh() const {
    return _mesh;
  }

  /**
   * Sets the Mesh object defining the mesh to be rendered.
   * @param mesh The mesh to render.
   */
  void setMesh(std::shared_ptr<Mesh> mesh);

  /**
   * Returns the list of fill styles used to fill the mesh. Each style contains a shader, alpha,
   * and blend mode. The fill styles are drawn in the order they are added. If the fill styles list
   * is empty, the mesh will be rendered with its vertex colors only. By default, the fill styles
   * list is empty. Note: If the mesh has vertex colors, they take priority over the fill style
   * colors. The fill style shader (if any) will be modulated with vertex colors.
   */
  const std::vector<std::shared_ptr<ShapeStyle>>& fillStyles() const {
    return _fillStyles;
  }

  /**
   * Replaces the current list of fill styles with the given list.
   */
  void setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills);

  /**
   * Removes all fill styles from the mesh layer.
   */
  void removeFillStyles();

  /**
   * Replace the current list of fill styles with the given fill style.
   */
  void setFillStyle(std::shared_ptr<ShapeStyle> fillStyle);

  /**
   * Adds a fill style to the end of the existing fill styles.
   */
  void addFillStyle(std::shared_ptr<ShapeStyle> fillStyle);

 protected:
  MeshLayer() = default;

  void onUpdateContent(LayerRecorder* recorder) override;

 private:
  std::shared_ptr<Mesh> _mesh = nullptr;
  std::vector<std::shared_ptr<ShapeStyle>> _fillStyles = {};
};
}  // namespace tgfx
