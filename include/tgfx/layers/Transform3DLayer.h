/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include "tgfx/layers/Layer.h"

namespace tgfx {

/**
 * Transform3DLayer is a layer that applies a 3D transformation to its content and all its children
 * layers. This layer renders all its child layers and its own content off-screen to a texture, then
 * applies a 3D projection transformation to that texture.
 */
class Transform3DLayer final : public Layer {
 public:
  static std::shared_ptr<Transform3DLayer> Make();

  LayerType type() const override {
    return LayerType::Transform3D;
  }

  /**
   * Returns the content layer of this layer.
   */
  std::shared_ptr<Layer> content() const {
    return _content;
  }

  /**
   * Sets the content layer of this layer.
   */
  void setContent(const std::shared_ptr<Layer>& content);

  /**
   * Returns the 3D transformation matrix. This matrix maps the layer's bounding rectangle model
   * from world coordinates to clip coordinates. When mapping the layer's final coordinates to
   * window coordinates, the layer's width and height will be used as the viewport size.
   */
  const Matrix3D& matrix3D() const {
    return _matrix3D;
  }

  /**
   * Sets the 3D transformation matrix.
   */
  void setMatrix3D(const Matrix3D& matrix3D);

 private:
  Transform3DLayer() = default;

  void onUpdateContent(LayerRecorder* recorder) override;

  std::shared_ptr<Layer> _content = nullptr;

  Matrix3D _matrix3D = Matrix3D::I();
};

}  // namespace tgfx