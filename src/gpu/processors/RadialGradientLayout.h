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

#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class RadialGradientLayout : public FragmentProcessor {
 public:
  static PlacementPtr<RadialGradientLayout> Make(BlockAllocator* allocator, Matrix matrix);

  std::string name() const override {
    return "RadialGradientLayout";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit RadialGradientLayout(Matrix matrix);

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  void onBuildShaderMacros(ShaderMacroSet& macros) const override {
    if (coordTransform.matrix.hasPerspective()) {
      macros.define("TGFX_RGRAD_PERSPECTIVE");
    }
  }

  std::string shaderFunctionFile() const override {
    return "fragment/radial_gradient.frag";
  }

  ShaderCallManifest buildCallStatement(const std::string& /*inputColorVar*/, int fpIndex,
                                        const MangledUniforms& /*uniforms*/,
                                        const MangledVaryings& varyings,
                                        const MangledSamplers& /*samplers*/) const override {
    ShaderCallManifest manifest;
    manifest.functionName = "TGFX_RadialGradientLayout";
    manifest.outputVarName = "color_fp" + std::to_string(fpIndex);
    manifest.includeFiles = {shaderFunctionFile()};
    manifest.argExpressions = {varyings.getCoordTransform(0)};
    return manifest;
  }

  CoordTransform coordTransform;
};
}  // namespace tgfx
