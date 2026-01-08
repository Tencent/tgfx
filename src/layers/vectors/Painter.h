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

#include <vector>
#include "Geometry.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

class LayerRecorder;

/**
 * Painter is the base class for objects that perform draw operations.
 */
class Painter {
 public:
  virtual ~Painter() = default;

  /**
   * Applies a transformation matrix and alpha multiplier to this painter.
   */
  void applyTransform(const Matrix& groupMatrix, float groupAlpha);

  /**
   * Offsets the geometry indices by the given amount.
   */
  void offsetGeometryIndex(size_t offset);

  /**
   * Draws the geometries to the given recorder.
   */
  void draw(LayerRecorder* recorder, const std::vector<std::unique_ptr<Geometry>>& geometries);

  /**
   * Creates a copy of this painter.
   */
  virtual std::unique_ptr<Painter> clone() const = 0;

  std::shared_ptr<Shader> shader = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  float alpha = 1.0f;
  Matrix matrix = Matrix::I();
  size_t startIndex = 0;
  std::vector<Matrix> matrices = {};

 protected:
  /**
   * Called for each geometry during draw. The innerMatrix is the matrix captured when the
   * painter was created, which should be applied before stroke and before the outer matrix.
   */
  virtual void onDraw(LayerRecorder* recorder, Geometry* geometry, const Matrix& innerMatrix) = 0;
};

}  // namespace tgfx
