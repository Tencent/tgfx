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

#include "layers/contents/GeometryContent.h"
#include "tgfx/core/Matrix.h"

namespace tgfx {

/**
 * MatrixContent wraps a GeometryContent and applies a transformation matrix during drawing.
 */
class MatrixContent : public GeometryContent {
 public:
  MatrixContent(std::unique_ptr<GeometryContent> content, const Matrix& matrix);

  Rect getBounds() const override;
  bool hasSameGeometry(const GeometryContent* other) const override;
  bool mayHaveSharpCorners() const override {
    return content->mayHaveSharpCorners();
  }
  const Color& getColor() const override;
  const std::shared_ptr<Shader>& getShader() const override;
  const BlendMode& getBlendMode() const override;
  Rect getTightBounds(const Matrix& matrix, const Stroke* stroke) const override;
  bool hitTestPoint(float localX, float localY, const Stroke* stroke) const override;
  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias,
                   const Stroke* stroke) const override;
  void drawForeground(Canvas* canvas, float alpha, bool antiAlias,
                      const Stroke* stroke) const override;
  void drawContour(Canvas* canvas, bool antiAlias, const Stroke* stroke) const override;
  bool contourEqualsOpaqueContent() const override;
  bool hasBlendMode() const override;

  std::unique_ptr<GeometryContent> content = nullptr;
  Matrix _matrix = Matrix::I();

 protected:
  Type type() const override {
    return Type::Matrix;
  }
};

}  // namespace tgfx
