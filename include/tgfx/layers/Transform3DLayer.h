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
   * Returns the 3D transformation matrix. This matrix transforms 3D model coordinates to
   * destination coordinates for x and y before perspective division. The z value is mapped to the
   * [-1, 1] range before perspective division; content outside this z range will be clipped.
   */
  const Matrix3D& matrix3D() const {
    return _matrix3D;
  }

  /**
   * Sets the 3D transformation matrix.
   */
  void setMatrix3D(const Matrix3D& matrix3D);

  /**
   * Returns whether to hide the back face of the content after the 3D transformation. The default
   * value is false, which means both the front and back faces are drawn.
   * When the layer is first created, the front face is oriented toward the user by default. After
   * applying certain 3D transformations, such as rotating 180 degrees around the X axis, the back
   * face of the layer may face the user.
   */
  bool hideBackFace() const {
    return _hideBackFace;
  }

  /**
   * Sets whether to hide the back face of the content after the 3D transformation.
   */
  void setHideBackFace(bool hideBackFace);

 private:
  Transform3DLayer() = default;

  void onUpdateContent(LayerRecorder* recorder) override;

  std::shared_ptr<Layer> _content = nullptr;

  Matrix3D _matrix3D = Matrix3D::I();

  bool _hideBackFace = false;
};

}  // namespace tgfx