/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include <optional>
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {
class GLSLQuadPerEdgeAAGeometryProcessor : public QuadPerEdgeAAGeometryProcessor {
 public:
  GLSLQuadPerEdgeAAGeometryProcessor(int width, int height, AAType aa,
                                     std::optional<Color> commonColor,
                                     std::optional<Matrix> uvMatrix, bool hasSubset);

  void emitCode(EmitArgs& args) const override;

  void setData(UniformBuffer* vertexUniformBuffer, UniformBuffer* fragmentUniformBuffer,
               FPCoordTransformIter* transformIter) const override;

  void onSetTransformData(UniformBuffer* uniformBuffer, const CoordTransform* coordTransform,
                          int index) const override;

  void onEmitTransform(EmitArgs& args, VertexShaderBuilder* vertexBuilder,
                       VaryingHandler* varyingHandler, UniformHandler* uniformHandler,
                       const std::string& transformUniformName, int index) const override;

 private:
  std::optional<std::string> subsetVaryingName = std::nullopt;
};
}  // namespace tgfx
