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

#include "tgfx/layers/filters/LayerFilter.h"

namespace tgfx {

/**
 * LayerPerspectiveType specifies the mode of perspective projection.
 *
 * Standard:
 * Represents the conventional perspective projection used in computer graphics.
 * In this mode, the projection model is established by defining the camera position, orientation,
 * field of view, and near/far planes. Points inside the view frustum are projected onto the near
 * plane.
 *
 * CSS:
 * Represents the perspective projection model inspired by CSS3 3D transforms.
 * In this mode, the projection plane is fixed at z=0, the camera orientation is fixed, and the
 * projection model is established by specifying the camera distance.
 * For more details on the definition of CSS perspective projection, please refer to the official
 * documentation: CSS Transforms Module Level 2, Perspective.
 */
enum class LayerPerspectiveType { Standard, CSS };

/**
 * LayerPerspectiveInfo specifies the parameters for perspective projection.
 */
struct LayerPerspectiveInfo {
  /**
   * The type of projection. The default is Standard.
   */
  LayerPerspectiveType projectType = LayerPerspectiveType::Standard;

  /**
   * Rotation angles (in degrees) around the X, Y, and Z axes, applied in order.
   */
  float xRotation = 0.0f;
  float yRotation = 0.0f;
  float zRotation = 0.0f;

  /**
   * The depth of the projected object, in pixels. A larger depth means the object is closer to the
   * viewer and appears larger; a smaller value means it is farther and appears smaller.
   */
  float depth = 0.0f;

  bool operator==(const LayerPerspectiveInfo& other) const {
    return xRotation == other.xRotation && yRotation == other.yRotation &&
           zRotation == other.zRotation && depth == other.depth && projectType == other.projectType;
  }
};

/**
 * PerspectiveFilter is a filter that applies a perspective transformation to the input layer.
 */
class PerspectiveFilter final : public LayerFilter {
 public:
  /**
   * Creates a PerspectiveFilter with the specified LayerPerspectiveInfo.
   */
  static std::shared_ptr<PerspectiveFilter> Make(const LayerPerspectiveInfo& info);

  /**
   * Returns the LayerPerspectiveInfo for this PerspectiveFilter.
   */
  const LayerPerspectiveInfo& info() const {
    return _info;
  }

  /**
   * Sets the LayerPerspectiveInfo for this PerspectiveFilter.
   */
  void setInfo(const LayerPerspectiveInfo& info);

  /**
   * Returns the rotation angle (in degrees) about the X axis.
   */
  float xRotation() const {
    return _info.xRotation;
  }

  /**
   * Sets the rotation angle (in degrees) about the X axis.
   */
  void setXRotation(float xRotation);

  /**
   * Returns the rotation angle (in degrees) about the Y axis.
   */
  float yRotation() const {
    return _info.yRotation;
  }

  /**
   * Sets the rotation angle (in degrees) about the Y axis.
   */
  void setYRotation(float yRotation);

  /**
   * Returns the rotation angle (in degrees) about the Z axis.
   */
  float zRotation() const {
    return _info.zRotation;
  }

  /**
   * Sets the rotation angle (in degrees) about the Z axis.
   */
  void setZRotation(float zRotation);

  /**
   * Returns the depth of the projected object, in pixels.
   */
  float depth() const {
    return _info.depth;
  }

  /**
   * Sets the depth of the projected object, in pixels.
   */
  void setDepth(float depth);

 private:
  explicit PerspectiveFilter(const LayerPerspectiveInfo& info);

  std::shared_ptr<ImageFilter> onCreateImageFilter(float scale) override;

  LayerPerspectiveInfo _info;
};

}  // namespace tgfx
