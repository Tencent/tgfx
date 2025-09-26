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

#include "gpu/processors/Transform3DGeometryProcessor.h"

namespace tgfx {

/**
 * The implementation of QuadPerEdgeAA3DGeometryProcessor using GLSL.
 */
class GLSLQuadPerEdgeAA3DGeometryProcessor final : public Transform3DGeometryProcessor {
 public:
  /**
   * Creates a GLSLQuadPerEdgeAA3DGeometryProcessor instance with the specified parameters.
   */
  explicit GLSLQuadPerEdgeAA3DGeometryProcessor(AAType aa, const Matrix3D& matrix,
                                                const Vec2& ndcScale, const Vec2& ndcOffset);

  void emitCode(EmitArgs& args) const override;

  void setData(UniformBuffer* vertexUniformBuffer, UniformBuffer* fragmentUniformBuffer,
               FPCoordTransformIter* transformIter) const override;

 private:
  Color defaultColor = Color::White();
};

}  // namespace tgfx