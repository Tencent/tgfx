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

#include "layers/processors/GlassShapeGeometryFragmentProcessor.h"

namespace tgfx {

class GLSLGlassSDFGeometryFragmentProcessor : public GlassSDFGeometryFragmentProcessor {
 public:
  GLSLGlassSDFGeometryFragmentProcessor(GlassShapeType shapeType,
                                        const GlassSDFGeometryParams& params);

  void emitCode(EmitArgs& args) const override;

 private:
  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;
};

class GLSLGlassUDFGeometryFragmentProcessor : public GlassUDFGeometryFragmentProcessor {
 public:
  GLSLGlassUDFGeometryFragmentProcessor(std::shared_ptr<TextureProxy> mask,
                                        std::shared_ptr<TextureProxy> edgeMask,
                                        const GlassUDFGeometryParams& params,
                                        bool enableEdgeLighting);

  void emitCode(EmitArgs& args) const override;

 private:
  void onSetData(UniformData* vertexUniformData, UniformData* fragmentUniformData) const override;
};

}  // namespace tgfx
